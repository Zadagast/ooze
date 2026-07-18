/*
 * Ooze screensaver — a non-grabbing animated overlay inside Mutter.
 *
 * Phase 1 provides the idle/input/theme plumbing and timeline seam for the
 * future Ooze Flow renderer.  The current visual is the shared Aqua wallpaper.
 */

#include "ooze-screensaver.h"
#include "ooze-plugin-priv.h"
#include "ooze-aqua-draw.h"
#include "ooze-lock.h"
#include "ooze-theme.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-idle-monitor.h>
#include <clutter/clutter.h>

#include <glib.h>

#define OOZE_SCREENSAVER_FADE_IN_MS   1200
#define OOZE_SCREENSAVER_FADE_OUT_MS   350
#define OOZE_SCREENSAVER_ARM_DELAY_MS  250
#define OOZE_SCREENSAVER_TIMELINE_MS 10000

typedef struct _OozeSaverMode OozeSaverMode;

struct _OozeSaverMode
{
  void (*start)  (OozePlugin *plugin);
  void (*step)   (OozePlugin *plugin, gdouble progress);
  void (*resize) (OozePlugin *plugin, int width, int height);
  void (*stop)   (OozePlugin *plugin);
};

static void ooze_screensaver_sync_watches (OozePlugin *plugin);

static void
ooze_screensaver_mode_start (OozePlugin *plugin G_GNUC_UNUSED)
{
}

static void
ooze_screensaver_mode_step (OozePlugin *plugin G_GNUC_UNUSED,
                            gdouble      progress G_GNUC_UNUSED)
{
}

static void
ooze_screensaver_mode_resize (OozePlugin *plugin G_GNUC_UNUSED,
                              int          width G_GNUC_UNUSED,
                              int          height G_GNUC_UNUSED)
{
}

static void
ooze_screensaver_mode_stop (OozePlugin *plugin G_GNUC_UNUSED)
{
}

static const OozeSaverMode ooze_screensaver_mode = {
  .start = ooze_screensaver_mode_start,
  .step = ooze_screensaver_mode_step,
  .resize = ooze_screensaver_mode_resize,
  .stop = ooze_screensaver_mode_stop,
};

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

static void
ooze_screensaver_refresh_wallpaper (OozePlugin *plugin)
{
  g_autoptr (ClutterContent) wallpaper = NULL;
  int width;
  int height;

  if (!plugin->screensaver_overlay)
    return;

  width = (int) clutter_actor_get_width (plugin->screensaver_overlay);
  height = (int) clutter_actor_get_height (plugin->screensaver_overlay);
  wallpaper = ooze_aqua_wallpaper_content (plugin->screensaver_overlay,
                                            width,
                                            height);
  if (wallpaper)
    clutter_actor_set_content (plugin->screensaver_overlay,
                               g_steal_pointer (&wallpaper));
}

static void
ooze_screensaver_on_theme_changed (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    ooze_screensaver_refresh_wallpaper (plugin);
}

static void
ooze_screensaver_on_new_frame (ClutterTimeline *timeline,
                               gint             msecs G_GNUC_UNUSED,
                               gpointer         user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (plugin->screensaver_active)
    ooze_screensaver_mode.step (plugin,
                                clutter_timeline_get_progress (timeline));
}

static gboolean
ooze_screensaver_hide_after_fade (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_fade_id = 0;
  if (!plugin->screensaver_active && plugin->screensaver_overlay)
    clutter_actor_hide (plugin->screensaver_overlay);

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

  if (plugin->screensaver_active && plugin->screensaver_input_armed)
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
ooze_screensaver_build (OozePlugin *plugin)
{
  ClutterActor *stage;
  ClutterActor *window_group;
  MetaDisplay *display;
  MetaCompositor *compositor;
  int width;
  int height;

  if (plugin->screensaver_overlay)
    return;

  stage = ooze_screensaver_get_stage (plugin);
  if (!stage)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  width = (int) clutter_actor_get_width (stage);
  height = (int) clutter_actor_get_height (stage);

  plugin->screensaver_overlay = clutter_actor_new ();
  clutter_actor_set_size (plugin->screensaver_overlay,
                          (gfloat) width,
                          (gfloat) height);
  clutter_actor_set_position (plugin->screensaver_overlay, 0.0f, 0.0f);
  clutter_actor_set_reactive (plugin->screensaver_overlay, FALSE);

  ooze_screensaver_refresh_wallpaper (plugin);
  clutter_actor_set_opacity (plugin->screensaver_overlay, 0);

  compositor = meta_display_get_compositor (display);
  window_group = meta_compositor_get_window_group (compositor);
  clutter_actor_add_child (stage, plugin->screensaver_overlay);
  clutter_actor_set_child_above_sibling (stage,
                                         plugin->screensaver_overlay,
                                         window_group);

  plugin->screensaver_timeline =
    clutter_timeline_new_for_actor (plugin->screensaver_overlay,
                                    OOZE_SCREENSAVER_TIMELINE_MS);
  clutter_timeline_set_repeat_count (plugin->screensaver_timeline, -1);
  g_signal_connect (plugin->screensaver_timeline, "new-frame",
                    G_CALLBACK (ooze_screensaver_on_new_frame), plugin);

  plugin->screensaver_stage_capture_id =
    g_signal_connect (stage, "captured-event",
                      G_CALLBACK (ooze_screensaver_on_stage_event), plugin);

  ooze_screensaver_mode.resize (plugin, width, height);
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

  plugin->screensaver_active = TRUE;
  plugin->screensaver_input_armed = FALSE;
  if (plugin->screensaver_arm_id)
    g_source_remove (plugin->screensaver_arm_id);
  plugin->screensaver_arm_id =
    g_timeout_add (OOZE_SCREENSAVER_ARM_DELAY_MS,
                   ooze_screensaver_arm_input,
                   plugin);

  if (plugin->screensaver_fade_id)
    {
      g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id = 0;
    }

  clutter_actor_remove_all_transitions (plugin->screensaver_overlay);
  clutter_actor_show (plugin->screensaver_overlay);
  clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
  clutter_actor_save_easing_state (plugin->screensaver_overlay);
  clutter_actor_set_easing_mode (plugin->screensaver_overlay,
                                 CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_easing_duration (plugin->screensaver_overlay,
                                     OOZE_SCREENSAVER_FADE_IN_MS);
  clutter_actor_set_opacity (plugin->screensaver_overlay, 255);
  clutter_actor_restore_easing_state (plugin->screensaver_overlay);

  ooze_screensaver_mode.start (plugin);
  clutter_timeline_rewind (plugin->screensaver_timeline);
  clutter_timeline_start (plugin->screensaver_timeline);
  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_dismiss (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!plugin->screensaver_active)
    return;

  plugin->screensaver_active = FALSE;
  plugin->screensaver_input_armed = FALSE;
  ooze_screensaver_mode.stop (plugin);

  if (plugin->screensaver_arm_id)
    {
      g_source_remove (plugin->screensaver_arm_id);
      plugin->screensaver_arm_id = 0;
    }

  if (plugin->screensaver_timeline)
    clutter_timeline_pause (plugin->screensaver_timeline);

  if (plugin->screensaver_overlay)
    {
      clutter_actor_remove_all_transitions (plugin->screensaver_overlay);
      clutter_actor_save_easing_state (plugin->screensaver_overlay);
      clutter_actor_set_easing_mode (plugin->screensaver_overlay,
                                     CLUTTER_EASE_IN_CUBIC);
      clutter_actor_set_easing_duration (plugin->screensaver_overlay,
                                         OOZE_SCREENSAVER_FADE_OUT_MS);
      clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
      clutter_actor_restore_easing_state (plugin->screensaver_overlay);

      if (plugin->screensaver_fade_id)
        g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id =
        g_timeout_add (OOZE_SCREENSAVER_FADE_OUT_MS + 30,
                       ooze_screensaver_hide_after_fade,
                       plugin);
    }

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

  ooze_theme_watch (NULL, ooze_screensaver_on_theme_changed, plugin);
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

  ooze_theme_unwatch (NULL, ooze_screensaver_on_theme_changed, plugin);

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

  g_clear_object (&plugin->screensaver_timeline);
  g_clear_pointer (&plugin->screensaver_overlay, clutter_actor_destroy);
  plugin->screensaver_active = FALSE;
  plugin->screensaver_input_armed = FALSE;
}
