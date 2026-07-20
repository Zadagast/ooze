/*
 * Ooze idle fade — the modern Wayland "screensaver".
 *
 * Like GNOME and KDE, Ooze does not run screensaver modules: on idle the
 * desktop smoothly fades to black (an opaque overlay on top of the
 * stage); the lock screen engages after its own delay, and on user
 * activity the overlay cross-fades back to the desktop.
 */

#include "ooze-screensaver.h"
#include "ooze-plugin-priv.h"
#include "ooze-lock.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-idle-monitor.h>
#include <meta/compositor.h>
#include <clutter/clutter.h>

#include <glib.h>

#define OOZE_SAVER_BLACK_MS        900  /* desktop -> black */
#define OOZE_SAVER_DISMISS_MS      700  /* black -> desktop */
#define OOZE_SAVER_ARM_DELAY_MS    250

static void ooze_screensaver_sync_watches (OozePlugin *plugin);

static ClutterActor *
ooze_screensaver_get_stage (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return NULL;

  backend = meta_context_get_backend (meta_display_get_context (display));
  return CLUTTER_ACTOR (meta_backend_get_stage (backend));
}

/* Explicit opacity transition: implicit Clutter animations are skipped
 * on actors that are not yet mapped, which made earlier fades snap. */
static void
ooze_screensaver_fade (ClutterActor *actor,
                       guint         from,
                       guint         to,
                       guint         duration_ms)
{
  ClutterTransition *fade;

  clutter_actor_remove_transition (actor, "saver-fade");
  clutter_actor_show (actor);
  clutter_actor_set_opacity (actor, from);

  fade = clutter_property_transition_new ("opacity");
  clutter_transition_set_from (fade, G_TYPE_UINT, from);
  clutter_transition_set_to (fade, G_TYPE_UINT, to);
  clutter_timeline_set_duration (CLUTTER_TIMELINE (fade), duration_ms);
  clutter_timeline_set_progress_mode (CLUTTER_TIMELINE (fade),
                                      CLUTTER_EASE_IN_OUT_SINE);
  clutter_actor_add_transition (actor, "saver-fade", fade);
  g_object_unref (fade);
}

/* --- Overlay and lifecycle ------------------------------------------- */

static void
ooze_screensaver_build (OozePlugin *plugin)
{
  ClutterActor *stage;
  CoglColor black;
  int width;
  int height;

  if (plugin->screensaver_overlay)
    return;

  stage = ooze_screensaver_get_stage (plugin);
  if (!stage)
    return;

  width = (int) clutter_actor_get_width (stage);
  height = (int) clutter_actor_get_height (stage);

  cogl_color_init_from_4f (&black, 0.0f, 0.0f, 0.0f, 1.0f);

  plugin->screensaver_overlay = clutter_actor_new ();
  clutter_actor_set_background_color (plugin->screensaver_overlay, &black);
  clutter_actor_set_size (plugin->screensaver_overlay,
                          (gfloat) width,
                          (gfloat) height);
  clutter_actor_set_position (plugin->screensaver_overlay, 0.0f, 0.0f);
  clutter_actor_set_reactive (plugin->screensaver_overlay, FALSE);
  clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
  clutter_actor_hide (plugin->screensaver_overlay);
  clutter_actor_add_child (stage, plugin->screensaver_overlay);
}

static void
ooze_screensaver_raise_overlay (OozePlugin *plugin)
{
  ClutterActor *stage = ooze_screensaver_get_stage (plugin);

  if (stage && plugin->screensaver_overlay)
    clutter_actor_set_child_above_sibling (stage,
                                           plugin->screensaver_overlay,
                                           NULL);
}

/* While the opaque overlay fully obscures the desktop, Mutter stops
 * painting the windows behind it.  Force them to repaint as the overlay
 * fades out so apps fade back in with it instead of popping in once it
 * is gone. */
static void
ooze_screensaver_repaint_desktop (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaCompositor *compositor;
  ClutterActor *window_group;
  ClutterActor *child;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return;

  compositor = meta_display_get_compositor (display);
  if (!compositor)
    return;

  window_group = meta_compositor_get_window_group (compositor);
  if (!window_group)
    return;

  clutter_actor_queue_redraw (window_group);
  for (child = clutter_actor_get_first_child (window_group);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    clutter_actor_queue_redraw (child);
}

/* The dismiss cross-fade has finished: hide the overlay. */
static gboolean
ooze_screensaver_finish_dismiss (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_fade_id = 0;
  if (!plugin->screensaver_active && plugin->screensaver_overlay)
    {
      clutter_actor_remove_transition (plugin->screensaver_overlay,
                                       "saver-fade");
      clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
      clutter_actor_hide (plugin->screensaver_overlay);
    }

  return G_SOURCE_REMOVE;
}

static gboolean
ooze_screensaver_arm_input (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_arm_id = 0;
  plugin->screensaver_input_armed = plugin->screensaver_active;
  return G_SOURCE_REMOVE;
}

static gboolean
ooze_screensaver_on_stage_event (ClutterActor *stage G_GNUC_UNUSED,
                                 ClutterEvent *event G_GNUC_UNUSED,
                                 gpointer      user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (plugin->screensaver_active && !plugin->locked &&
      plugin->screensaver_input_armed)
    {
      ooze_screensaver_dismiss (plugin);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
ooze_screensaver_on_user_active (MetaIdleMonitor *monitor G_GNUC_UNUSED,
                                 guint            watch_id G_GNUC_UNUSED,
                                 gpointer         user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_user_active_watch_id = 0;
  if (plugin->locked)
    return;

  if (plugin->screensaver_active)
    ooze_screensaver_dismiss (plugin);
  else
    ooze_screensaver_sync_watches (plugin);
}

static void
ooze_screensaver_on_idle (MetaIdleMonitor *monitor G_GNUC_UNUSED,
                          guint            watch_id G_GNUC_UNUSED,
                          gpointer         user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_idle_watch_id = 0;
  ooze_screensaver_activate (plugin);
}

static void
ooze_screensaver_on_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                      const char  *key G_GNUC_UNUSED,
                                      gpointer     user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    ooze_screensaver_sync_watches (plugin);
}

static void
ooze_screensaver_sync_watches (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;
  guint delay_sec = 0;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return;

  backend = meta_context_get_backend (meta_display_get_context (display));
  monitor = meta_backend_get_core_idle_monitor (backend);
  if (!monitor)
    return;

  if (plugin->screensaver_idle_watch_id)
    {
      meta_idle_monitor_remove_watch (monitor,
                                      plugin->screensaver_idle_watch_id);
      plugin->screensaver_idle_watch_id = 0;
    }
  if (plugin->screensaver_user_active_watch_id)
    {
      meta_idle_monitor_remove_watch (monitor,
                                      plugin->screensaver_user_active_watch_id);
      plugin->screensaver_user_active_watch_id = 0;
    }

  if (plugin->session_settings)
    delay_sec = g_settings_get_uint (plugin->session_settings, "idle-delay");

  if (plugin->locked)
    return;

  plugin->screensaver_user_active_watch_id =
    meta_idle_monitor_add_user_active_watch (monitor,
                                             ooze_screensaver_on_user_active,
                                             plugin,
                                             NULL);

  if (!plugin->screensaver_active && delay_sec > 0)
    plugin->screensaver_idle_watch_id =
      meta_idle_monitor_add_idle_watch (monitor,
                                        (guint64) delay_sec * 1000ull,
                                        ooze_screensaver_on_idle,
                                        plugin,
                                        NULL);
}

static void
ooze_screensaver_ensure_capture (OozePlugin *plugin)
{
  ClutterActor *stage;

  if (plugin->screensaver_stage_capture_id)
    return;

  stage = ooze_screensaver_get_stage (plugin);
  if (!stage)
    return;

  plugin->screensaver_stage_capture_id =
    g_signal_connect (stage, "captured-event",
                      G_CALLBACK (ooze_screensaver_on_stage_event), plugin);
}

void
ooze_screensaver_activate (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (plugin->shutting_down || plugin->locked ||
      plugin->screensaver_active)
    return;

  ooze_screensaver_build (plugin);
  if (!plugin->screensaver_overlay)
    return;
  ooze_screensaver_ensure_capture (plugin);
  ooze_screensaver_raise_overlay (plugin);

  plugin->screensaver_active = TRUE;
  plugin->screensaver_input_armed = FALSE;
  if (plugin->screensaver_arm_id)
    g_source_remove (plugin->screensaver_arm_id);
  plugin->screensaver_arm_id =
    g_timeout_add (OOZE_SAVER_ARM_DELAY_MS,
                   ooze_screensaver_arm_input,
                   plugin);
  if (plugin->screensaver_fade_id)
    {
      g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id = 0;
    }

  ooze_screensaver_fade (plugin->screensaver_overlay, 0u, 255u,
                         OOZE_SAVER_BLACK_MS);

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_lock_backdrop (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (plugin->shutting_down)
    return;

  ooze_screensaver_build (plugin);
  if (!plugin->screensaver_overlay)
    return;
  ooze_screensaver_ensure_capture (plugin);
  ooze_screensaver_raise_overlay (plugin);

  if (plugin->screensaver_fade_id)
    {
      g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id = 0;
    }

  plugin->screensaver_input_armed = FALSE;
  if (!plugin->screensaver_active)
    {
      plugin->screensaver_active = TRUE;
      clutter_actor_remove_transition (plugin->screensaver_overlay,
                                       "saver-fade");
      clutter_actor_show (plugin->screensaver_overlay);
      clutter_actor_set_opacity (plugin->screensaver_overlay, 255);
    }

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_unlock_backdrop (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;

  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!plugin->screensaver_active)
    {
      ooze_screensaver_rearm (plugin);
      return;
    }

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    {
      ooze_screensaver_dismiss (plugin);
      return;
    }

  backend = meta_context_get_backend (meta_display_get_context (display));
  monitor = meta_backend_get_core_idle_monitor (backend);
  if (!monitor || meta_idle_monitor_get_idletime (monitor) < 1000)
    ooze_screensaver_dismiss (plugin);
  else
    ooze_screensaver_rearm (plugin);
}

void
ooze_screensaver_dismiss (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!plugin->screensaver_active)
    return;

  plugin->screensaver_active = FALSE;
  plugin->screensaver_input_armed = FALSE;

  if (plugin->screensaver_arm_id)
    {
      g_source_remove (plugin->screensaver_arm_id);
      plugin->screensaver_arm_id = 0;
    }

  /* Fade the black away so the desktop eases back in. */
  if (plugin->screensaver_overlay)
    {
      guint from = clutter_actor_get_opacity (plugin->screensaver_overlay);

      ooze_screensaver_repaint_desktop (plugin);
      ooze_screensaver_fade (plugin->screensaver_overlay, from, 0u,
                             OOZE_SAVER_DISMISS_MS);
    }

  if (plugin->screensaver_fade_id)
    g_source_remove (plugin->screensaver_fade_id);
  plugin->screensaver_fade_id =
    g_timeout_add (OOZE_SAVER_DISMISS_MS + 40,
                   ooze_screensaver_finish_dismiss,
                   plugin);

  ooze_screensaver_sync_watches (plugin);
}

gboolean
ooze_screensaver_is_active (OozePlugin *plugin)
{
  return plugin && plugin->screensaver_active;
}

void
ooze_screensaver_rearm (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_init (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  g_signal_connect (plugin->session_settings, "changed::idle-delay",
                    G_CALLBACK (ooze_screensaver_on_settings_changed), plugin);
  g_signal_connect (plugin->screensaver_settings, "changed::lock-enabled",
                    G_CALLBACK (ooze_screensaver_on_settings_changed), plugin);
  g_signal_connect (plugin->screensaver_settings, "changed::lock-delay",
                    G_CALLBACK (ooze_screensaver_on_settings_changed), plugin);

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_dispose (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;

  if (!plugin)
    return;

  if (plugin->screensaver_arm_id)
    {
      g_source_remove (plugin->screensaver_arm_id);
      plugin->screensaver_arm_id = 0;
    }
  if (plugin->screensaver_fade_id)
    {
      g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id = 0;
    }

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (display)
    {
      backend = meta_context_get_backend (meta_display_get_context (display));
      monitor = meta_backend_get_core_idle_monitor (backend);
      if (monitor)
        {
          if (plugin->screensaver_idle_watch_id)
            meta_idle_monitor_remove_watch (
              monitor, plugin->screensaver_idle_watch_id);
          if (plugin->screensaver_user_active_watch_id)
            meta_idle_monitor_remove_watch (
              monitor, plugin->screensaver_user_active_watch_id);
        }
    }
  plugin->screensaver_idle_watch_id = 0;
  plugin->screensaver_user_active_watch_id = 0;

  if (plugin->screensaver_stage_capture_id)
    {
      ClutterActor *stage = ooze_screensaver_get_stage (plugin);

      if (stage)
        g_clear_signal_handler (&plugin->screensaver_stage_capture_id, stage);
      else
        plugin->screensaver_stage_capture_id = 0;
    }

  g_clear_pointer (&plugin->screensaver_overlay, clutter_actor_destroy);
  plugin->screensaver_active = FALSE;
  plugin->screensaver_input_armed = FALSE;
}
