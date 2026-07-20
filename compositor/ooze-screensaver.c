/*
 * Ooze screensaver — XScreenSaver modules hosted by the compositor.
 *
 * The lifecycle mirrors how classic desktops run screensavers:
 *
 *   1. On idle the desktop fades to black (an opaque overlay on top of
 *      the stage).
 *   2. The selected XScreenSaver module — an X11 program running under
 *      Xwayland — is spawned; when its window maps it is adopted into
 *      the overlay and fades in over the black.
 *   3. On user activity the whole overlay cross-fades back to the
 *      desktop, then the module is killed.
 *
 * If no module is configured or it is missing/dies, the saver simply
 * stays black (classic blank mode).
 */

#include "ooze-screensaver.h"
#include "ooze-plugin-priv.h"
#include "ooze-lock.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-idle-monitor.h>
#include <meta/compositor.h>
#include <meta/window.h>
#include <clutter/clutter.h>

#include <glib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#define OOZE_SAVER_BLACK_MS        900  /* desktop -> black */
#define OOZE_SAVER_REVEAL_MS       900  /* black -> module */
#define OOZE_SAVER_DISMISS_MS      700  /* saver -> desktop */
#define OOZE_SAVER_ARM_DELAY_MS    250

static void ooze_screensaver_sync_watches (OozePlugin *plugin);
static void ooze_screensaver_hack_start (OozePlugin *plugin);

static char *
ooze_screensaver_current_mode (OozePlugin *plugin)
{
  if (!plugin->scenery_settings)
    return g_strdup ("none");

  return g_settings_get_string (plugin->scenery_settings,
                                "screensaver-mode");
}

static gboolean
ooze_screensaver_enabled (OozePlugin *plugin)
{
  g_autofree char *mode = ooze_screensaver_current_mode (plugin);

  return g_strcmp0 (mode, "none") != 0;
}

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

/* --- XScreenSaver module process ------------------------------------- */

static const char *
ooze_screensaver_mode_hack (const char *mode)
{
  if (mode == NULL || !g_str_has_prefix (mode, "xscreensaver:"))
    return NULL;

  return mode + strlen ("xscreensaver:");
}

static char *
ooze_screensaver_hack_binary (const char *hack)
{
  static const char *dirs[] = {
    "/usr/libexec/xscreensaver",
    "/usr/lib/xscreensaver",
    "/usr/lib/misc/xscreensaver",
  };
  gsize i;

  if (hack == NULL || *hack == '\0')
    return NULL;

  /* Only plain program names: never let the setting name a path. */
  for (i = 0; hack[i] != '\0'; i++)
    if (!g_ascii_isalnum (hack[i]) && hack[i] != '-' && hack[i] != '_')
      return NULL;

  for (i = 0; i < G_N_ELEMENTS (dirs); i++)
    {
      char *path = g_build_filename (dirs[i], hack, NULL);

      if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
        return path;
      g_free (path);
    }

  return NULL;
}

static void
ooze_screensaver_set_unredirect (OozePlugin *plugin,
                                 gboolean    disabled)
{
  MetaDisplay *display;
  MetaCompositor *compositor;

  if (disabled == plugin->saver_unredirect_disabled)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return;

  compositor = meta_display_get_compositor (display);
  if (!compositor)
    return;

  if (disabled)
    meta_compositor_disable_unredirect (compositor);
  else
    meta_compositor_enable_unredirect (compositor);
  plugin->saver_unredirect_disabled = disabled;
}

static void
ooze_screensaver_hack_cleanup (OozePlugin *plugin,
                               gboolean    kill_process)
{
  if (plugin->saver_hack_kill_id)
    {
      g_source_remove (plugin->saver_hack_kill_id);
      plugin->saver_hack_kill_id = 0;
    }
  if (plugin->saver_hack_child_watch_id)
    {
      g_source_remove (plugin->saver_hack_child_watch_id);
      plugin->saver_hack_child_watch_id = 0;
    }
  if (plugin->saver_hack_pid)
    {
      if (kill_process)
        kill (plugin->saver_hack_pid, SIGTERM);
      g_spawn_close_pid (plugin->saver_hack_pid);
      plugin->saver_hack_pid = 0;
    }
  g_clear_pointer (&plugin->saver_hack_name, g_free);
  g_clear_pointer (&plugin->saver_hack_clone, clutter_actor_destroy);
  plugin->saver_hack_window = NULL;
  ooze_screensaver_set_unredirect (plugin, FALSE);
}

static void
ooze_screensaver_hack_child_exited (GPid     pid,
                                    gint     status G_GNUC_UNUSED,
                                    gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->saver_hack_child_watch_id = 0;
  if (plugin->saver_hack_pid == pid)
    {
      g_spawn_close_pid (pid);
      plugin->saver_hack_pid = 0;
      /* The module died: the saver stays black (classic blank mode). */
      ooze_screensaver_hack_cleanup (plugin, FALSE);
    }
}

static void
ooze_screensaver_hack_start (OozePlugin *plugin)
{
  g_autofree char *mode = ooze_screensaver_current_mode (plugin);
  const char *hack = ooze_screensaver_mode_hack (mode);
  g_autofree char *binary = NULL;
  g_autoptr (GError) error = NULL;
  char *argv[3];
  GPid pid = 0;

  ooze_screensaver_hack_cleanup (plugin, TRUE);

  if (!hack)
    return;

  binary = ooze_screensaver_hack_binary (hack);
  if (!binary)
    {
      g_warning ("Ooze screensaver: XScreenSaver module '%s' not found "
                 "(install the xscreensaver-data/-gl packages); "
                 "staying blank",
                 hack);
      return;
    }

  argv[0] = binary;
  argv[1] = (char *) "--window";
  argv[2] = NULL;
  if (!g_spawn_async (NULL, argv, NULL,
                      G_SPAWN_DO_NOT_REAP_CHILD,
                      NULL, NULL, &pid, &error))
    {
      g_warning ("Ooze screensaver: unable to launch %s: %s",
                 binary, error->message);
      return;
    }

  plugin->saver_hack_pid = pid;
  plugin->saver_hack_name = g_strdup (hack);
  plugin->saver_hack_child_watch_id =
    g_child_watch_add (pid, ooze_screensaver_hack_child_exited, plugin);

  /* A fullscreen module window would otherwise be unredirected (direct
   * scan-out), which stops the compositor updating the texture we clone,
   * freezing the saver after a few frames. */
  ooze_screensaver_set_unredirect (plugin, TRUE);
}

static gboolean
ooze_screensaver_window_is_hack (OozePlugin *plugin,
                                 MetaWindow *window)
{
  const char *wm_class;
  const char *wm_instance;

  if (meta_window_get_pid (window) == (pid_t) plugin->saver_hack_pid)
    return TRUE;

  /* X11 clients under Xwayland may not report a usable pid; the modules
   * all set their WM_CLASS to the module name. */
  if (!plugin->saver_hack_name)
    return FALSE;

  wm_class = meta_window_get_wm_class (window);
  wm_instance = meta_window_get_wm_class_instance (window);
  return (wm_class &&
          g_ascii_strcasecmp (wm_class, plugin->saver_hack_name) == 0) ||
         (wm_instance &&
          g_ascii_strcasecmp (wm_instance, plugin->saver_hack_name) == 0);
}

gboolean
ooze_screensaver_adopt_hack_window (OozePlugin      *plugin,
                                    MetaWindowActor *actor)
{
  MetaWindow *window;
  ClutterActor *clone;
  int width;
  int height;

  if (!plugin->saver_hack_pid || plugin->saver_hack_clone ||
      !plugin->screensaver_overlay)
    return FALSE;

  window = meta_window_actor_get_meta_window (actor);
  if (!window || !ooze_screensaver_window_is_hack (plugin, window))
    return FALSE;

  width = (int) clutter_actor_get_width (plugin->screensaver_overlay);
  height = (int) clutter_actor_get_height (plugin->screensaver_overlay);

  meta_window_make_fullscreen (window);
  meta_window_move_resize_frame (window, FALSE, 0, 0, width, height);
  clutter_actor_hide (CLUTTER_ACTOR (actor));

  clone = clutter_clone_new (CLUTTER_ACTOR (actor));
  clutter_actor_set_position (clone, 0.0f, 0.0f);
  clutter_actor_set_size (clone, (gfloat) width, (gfloat) height);
  clutter_actor_set_opacity (clone, 0);
  clutter_actor_add_child (plugin->screensaver_overlay, clone);
  plugin->saver_hack_clone = clone;
  plugin->saver_hack_window = window;

  /* Once the screen is fully black, fade the module in over it.  If we
   * are still fading to black the reveal timer does this instead. */
  if (plugin->screensaver_active && plugin->screensaver_black)
    ooze_screensaver_fade (clone, 0u, 255u, OOZE_SAVER_REVEAL_MS);

  return TRUE;
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

/* The desktop has finished fading to black: reveal the module if it has
 * already mapped (otherwise adoption reveals it when it does). */
static gboolean
ooze_screensaver_on_black (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_phase_id = 0;
  plugin->screensaver_black = TRUE;

  if (plugin->screensaver_active && plugin->saver_hack_clone)
    ooze_screensaver_fade (plugin->saver_hack_clone, 0u, 255u,
                           OOZE_SAVER_REVEAL_MS);
  return G_SOURCE_REMOVE;
}

/* The dismiss cross-fade has finished: hide the overlay and kill the
 * module. */
static gboolean
ooze_screensaver_finish_dismiss (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_fade_id = 0;
  if (!plugin->screensaver_active)
    {
      ooze_screensaver_hack_cleanup (plugin, TRUE);
      if (plugin->screensaver_overlay)
        {
          clutter_actor_remove_transition (plugin->screensaver_overlay,
                                           "saver-fade");
          clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
          clutter_actor_hide (plugin->screensaver_overlay);
        }
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
  if (!ooze_screensaver_enabled (plugin))
    {
      ooze_screensaver_sync_watches (plugin);
      return;
    }

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
      !ooze_screensaver_enabled (plugin) ||
      plugin->screensaver_active)
    return;

  ooze_screensaver_build (plugin);
  if (!plugin->screensaver_overlay)
    return;
  ooze_screensaver_ensure_capture (plugin);
  ooze_screensaver_raise_overlay (plugin);

  plugin->screensaver_active = TRUE;
  plugin->screensaver_black = FALSE;
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
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
    }

  /* Spawn the module now so it is ready behind the black. */
  ooze_screensaver_hack_start (plugin);

  /* Phase 1: fade the desktop to black. */
  ooze_screensaver_fade (plugin->screensaver_overlay, 0u, 255u,
                         OOZE_SAVER_BLACK_MS);
  plugin->screensaver_phase_id =
    g_timeout_add (OOZE_SAVER_BLACK_MS + 30,
                   ooze_screensaver_on_black,
                   plugin);

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_lock_backdrop (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (plugin->shutting_down || !ooze_screensaver_enabled (plugin))
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
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
    }

  plugin->screensaver_input_armed = FALSE;
  if (!plugin->screensaver_active)
    {
      plugin->screensaver_active = TRUE;
      plugin->screensaver_black = TRUE;
      clutter_actor_remove_transition (plugin->screensaver_overlay,
                                       "saver-fade");
      clutter_actor_show (plugin->screensaver_overlay);
      clutter_actor_set_opacity (plugin->screensaver_overlay, 255);
      if (!plugin->saver_hack_pid)
        ooze_screensaver_hack_start (plugin);
      if (plugin->saver_hack_clone)
        clutter_actor_set_opacity (plugin->saver_hack_clone, 255);
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

  if (!ooze_screensaver_enabled (plugin))
    {
      ooze_screensaver_dismiss (plugin);
      return;
    }

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
  plugin->screensaver_black = FALSE;
  plugin->screensaver_input_armed = FALSE;

  if (plugin->screensaver_arm_id)
    {
      g_source_remove (plugin->screensaver_arm_id);
      plugin->screensaver_arm_id = 0;
    }
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
    }

  /* Cross-fade the whole saver back to the desktop; the module keeps
   * animating through the fade and is killed once it completes. */
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
ooze_screensaver_refresh_wallpaper (OozePlugin *plugin G_GNUC_UNUSED)
{
  /* The saver no longer shows the wallpaper; nothing to refresh. */
}

void
ooze_screensaver_mode_changed (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!ooze_screensaver_enabled (plugin) && plugin->screensaver_active)
    {
      ooze_screensaver_dismiss (plugin);
      return;
    }

  /* Swap the module live if the saver is already on screen. */
  if (plugin->screensaver_active)
    {
      g_autofree char *mode = ooze_screensaver_current_mode (plugin);
      const char *hack = ooze_screensaver_mode_hack (mode);

      if (g_strcmp0 (hack, plugin->saver_hack_name) != 0)
        ooze_screensaver_hack_start (plugin);
    }

  ooze_screensaver_sync_watches (plugin);
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
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
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

  ooze_screensaver_hack_cleanup (plugin, TRUE);

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
  plugin->screensaver_black = FALSE;
  plugin->screensaver_input_armed = FALSE;
}
