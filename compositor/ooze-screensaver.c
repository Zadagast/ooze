/*
 * Ooze screensaver — a non-grabbing animated overlay inside Mutter.
 *
 * Ooze Flow is rendered by a Cogl fragment shader over the shared wallpaper,
 * with a full-resolution Cairo renderer retained as a fallback.
 */

#include "ooze-screensaver.h"
#include "ooze-plugin-priv.h"
#include "ooze-aqua-draw.h"
#include "ooze-flow.h"
#include "ooze-flow-gpu.h"
#include "ooze-lock.h"
#include "ooze-wallpaper.h"
#include "ooze-theme.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-idle-monitor.h>
#include <meta/compositor.h>
#include <meta/window.h>
#include <clutter/clutter.h>

#include <glib.h>
#include <cairo/cairo.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#define OOZE_SCREENSAVER_FADE_IN_MS   1600
#define OOZE_SCREENSAVER_FADE_OUT_MS   1100
#define OOZE_SCREENSAVER_ARM_DELAY_MS  250
#define OOZE_SCREENSAVER_TIMELINE_MS 10000
#define OOZE_SAVER_PHASE_MS            750

typedef struct _OozeSaverMode OozeSaverMode;

typedef struct
{
  cairo_surface_t *surface;
  int              width;
  int              height;
  int              render_width;
  int              render_height;
  gdouble          phase;
  gint64           phase_start_us;
  gint64           last_render_us;
  OozeFlowGpu     *gpu;
  char            *scene;
} OozeFlowState;

struct _OozeSaverMode
{
  void (*start)  (OozePlugin *plugin);
  void (*step)   (OozePlugin *plugin, gdouble progress);
  void (*resize) (OozePlugin *plugin, int width, int height);
  void (*stop)   (OozePlugin *plugin);
};

static void ooze_screensaver_sync_watches (OozePlugin *plugin);
static void ooze_screensaver_mode_resize (OozePlugin *plugin,
                                          int          width,
                                          int          height);

static char *
ooze_screensaver_current_scene (OozePlugin *plugin)
{
  if (!plugin->scenery_settings)
    return g_strdup ("flow");

  return g_settings_get_string (plugin->scenery_settings,
                                "screensaver-mode");
}

static gboolean
ooze_screensaver_flow_enabled (OozePlugin *plugin)
{
  g_autofree char *mode = ooze_screensaver_current_scene (plugin);

  return g_strcmp0 (mode, "none") != 0;
}

static OozeFlowState *
ooze_screensaver_get_flow (OozePlugin *plugin)
{
  return plugin->screensaver_flow;
}

static void
ooze_screensaver_flow_render (OozePlugin *plugin,
                              gboolean    force)
{
  OozeFlowState *flow = ooze_screensaver_get_flow (plugin);
  const OozeAquaPalette *palette;
  gboolean dark;
  gdouble red;
  gdouble green;
  gdouble blue;
  gint64 now;

  if (!flow)
    return;

  now = g_get_monotonic_time ();
  if (!force && flow->last_render_us != 0 &&
      now - flow->last_render_us < 33000)
    return;

  dark = ooze_theme_is_dark (NULL);
  palette = ooze_theme_get_palette (NULL);
  red = 0.10 + palette->wallpaper_edge_r * 0.25;
  green = 0.24 + palette->wallpaper_mid_g * 0.25;
  blue = 0.58 + palette->wallpaper_mid_b * 0.25;
  if (flow->gpu)
    {
      ooze_flow_gpu_set_phase (flow->gpu, flow->phase);
      ooze_flow_gpu_set_color (flow->gpu,
                               CLAMP (red, 0.0, 1.0),
                               CLAMP (green, 0.0, 1.0),
                               CLAMP (blue, 0.0, 1.0),
                               dark);
      clutter_actor_queue_redraw (plugin->screensaver_flow_actor);
    }
  else if (flow->surface)
    {
      if (flow->scene && g_strcmp0 (flow->scene, "flow") != 0 &&
          g_strcmp0 (flow->scene, "none") != 0)
        ooze_flow_render_scene (flow->surface,
                                flow->render_width,
                                flow->render_height,
                                flow->phase,
                                dark,
                                flow->scene);
      else
        ooze_flow_render_with_color (flow->surface,
                                     flow->render_width,
                                     flow->render_height,
                                     flow->phase,
                                     dark,
                                     CLAMP (red, 0.0, 1.0),
                                     CLAMP (green, 0.0, 1.0),
                                     CLAMP (blue, 0.0, 1.0));

      {
        g_autoptr (ClutterContent) content = NULL;

        content = ooze_aqua_content_from_surface (
          plugin->screensaver_flow_actor,
          flow->surface);
        if (content)
          clutter_actor_set_content (plugin->screensaver_flow_actor,
                                     g_steal_pointer (&content));
      }
    }
  flow->last_render_us = now;
}

static void
ooze_screensaver_flow_free (OozePlugin *plugin)
{
  OozeFlowState *flow = ooze_screensaver_get_flow (plugin);

  if (!flow)
    return;

  ooze_flow_gpu_free (flow->gpu);
  g_clear_pointer (&flow->surface, cairo_surface_destroy);
  g_free (flow->scene);
  g_free (flow);
  plugin->screensaver_flow = NULL;
}

/* --- XScreenSaver hack backdrop ---------------------------------------
 *
 * Modes of the form "xscreensaver:<hack>" spawn the matching XScreenSaver
 * hack (an X11 program running under Xwayland) in windowed mode.  When its
 * window maps, the plugin map vfunc hands it to
 * ooze_screensaver_adopt_hack_window(): the real window actor is hidden and
 * a fullscreen ClutterClone of it is placed inside the saver overlay, so
 * the existing fade/lock lifecycle keeps working.  Ooze Flow renders
 * underneath until the hack's first frame arrives, and again if the hack
 * dies.
 */

static const char *
ooze_screensaver_scene_hack (const char *scene)
{
  if (scene == NULL || !g_str_has_prefix (scene, "xscreensaver:"))
    return NULL;

  return scene + strlen ("xscreensaver:");
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
  if (plugin->screensaver_flow_actor)
    clutter_actor_show (plugin->screensaver_flow_actor);
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
      /* Fall back to Flow underneath if the hack dies mid-saver. */
      ooze_screensaver_hack_cleanup (plugin, FALSE);
    }
}

static void
ooze_screensaver_hack_start (OozePlugin *plugin,
                             const char *scene)
{
  const char *hack = ooze_screensaver_scene_hack (scene);
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
      g_warning ("Ooze screensaver: XScreenSaver hack '%s' not found "
                 "(install xscreensaver-data/-extra); showing Ooze Flow",
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

  /* A fullscreen hack window would otherwise be unredirected (direct
   * scan-out), which stops the compositor updating the texture we clone,
   * freezing the saver after a few frames. */
  ooze_screensaver_set_unredirect (plugin, TRUE);
}

static gboolean
ooze_screensaver_hack_kill_after_fade (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->saver_hack_kill_id = 0;
  ooze_screensaver_hack_cleanup (plugin, TRUE);
  return G_SOURCE_REMOVE;
}

static void
ooze_screensaver_hack_stop (OozePlugin *plugin)
{
  if (!plugin->saver_hack_pid && !plugin->saver_hack_clone)
    return;

  /* Keep the clone painting through the overlay fade-out. */
  if (plugin->saver_hack_kill_id)
    g_source_remove (plugin->saver_hack_kill_id);
  plugin->saver_hack_kill_id =
    g_timeout_add (OOZE_SCREENSAVER_FADE_OUT_MS + 60,
                   ooze_screensaver_hack_kill_after_fade,
                   plugin);
}

static gboolean
ooze_screensaver_window_is_hack (OozePlugin *plugin,
                                 MetaWindow *window)
{
  const char *wm_class;
  const char *wm_instance;

  if (meta_window_get_pid (window) == (pid_t) plugin->saver_hack_pid)
    return TRUE;

  /* X11 clients under Xwayland may not report a usable pid; the hacks
   * all set their WM_CLASS to the hack name. */
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
  clutter_actor_add_child (plugin->screensaver_overlay, clone);
  plugin->saver_hack_clone = clone;
  plugin->saver_hack_window = window;

  if (plugin->screensaver_flow_actor)
    clutter_actor_hide (plugin->screensaver_flow_actor);

  return TRUE;
}

static void
ooze_screensaver_mode_start (OozePlugin *plugin G_GNUC_UNUSED)
{
  OozeFlowState *flow;

  if (!plugin->screensaver_flow_actor)
    {
      plugin->screensaver_flow_actor = clutter_actor_new ();
      clutter_actor_set_position (plugin->screensaver_flow_actor, 0.0f, 0.0f);
      clutter_actor_set_reactive (plugin->screensaver_flow_actor, FALSE);
      clutter_actor_add_child (plugin->screensaver_overlay,
                               plugin->screensaver_flow_actor);
    }

  flow = ooze_screensaver_get_flow (plugin);
  if (!flow)
    {
      ooze_screensaver_mode_resize (plugin,
                                    (int) clutter_actor_get_width (
                                      plugin->screensaver_overlay),
                                    (int) clutter_actor_get_height (
                                      plugin->screensaver_overlay));
      flow = ooze_screensaver_get_flow (plugin);
    }

  if (flow)
    {
      flow->phase = 0.0;
      flow->phase_start_us = g_get_monotonic_time ();
      flow->last_render_us = 0;
      ooze_screensaver_flow_render (plugin, TRUE);
    }

  ooze_screensaver_hack_start (plugin, flow ? flow->scene : NULL);
}

static void
ooze_screensaver_mode_step (OozePlugin *plugin G_GNUC_UNUSED,
                            gdouble      progress G_GNUC_UNUSED)
{
  OozeFlowState *flow = ooze_screensaver_get_flow (plugin);
  gint64 now;

  if (!flow)
    return;

  now = g_get_monotonic_time ();
  if (flow->phase_start_us == 0)
    flow->phase_start_us = now;
  /* ~26s per base cycle; per-blob speeds stretch this to a very slow,
   * lava-lamp rise/sink of roughly 36-65s per blob. */
  flow->phase = (now - flow->phase_start_us) *
                (G_PI * 2.0 / 26000000.0);
  ooze_screensaver_flow_render (plugin, FALSE);
}

static void
ooze_screensaver_mode_resize (OozePlugin *plugin,
                              int          width,
                              int          height)
{
  OozeFlowState *flow;
  int render_width;
  int render_height;

  if (!plugin->screensaver_flow_actor)
    return;

  clutter_actor_set_size (plugin->screensaver_flow_actor,
                          (gfloat) width,
                          (gfloat) height);
  render_width = MAX (1, width);
  render_height = MAX (1, height);

  clutter_actor_set_content (plugin->screensaver_flow_actor, NULL);
  ooze_screensaver_flow_free (plugin);
  flow = g_new0 (OozeFlowState, 1);
  flow->width = width;
  flow->height = height;
  flow->render_width = render_width;
  flow->render_height = render_height;
  flow->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                              render_width,
                                              render_height);
  flow->scene = ooze_screensaver_current_scene (plugin);
  flow->gpu = ooze_flow_gpu_new_for_scene (flow->scene);
  if (flow->gpu)
    {
      g_autoptr (ClutterContent) content = NULL;

      ooze_flow_gpu_set_size (flow->gpu, width, height);
      content = g_object_ref (ooze_flow_gpu_get_content (flow->gpu));
      clutter_actor_set_content (plugin->screensaver_flow_actor,
                                 g_steal_pointer (&content));
    }
  plugin->screensaver_flow = flow;
}

static void
ooze_screensaver_mode_stop (OozePlugin *plugin G_GNUC_UNUSED)
{
  ooze_screensaver_hack_stop (plugin);
  ooze_screensaver_flow_free (plugin);
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

void
ooze_screensaver_refresh_wallpaper (OozePlugin *plugin)
{
  g_autoptr (ClutterContent) wallpaper = NULL;
  int width;
  int height;

  if (!plugin->screensaver_overlay)
    return;

  width = (int) clutter_actor_get_width (plugin->screensaver_overlay);
  height = (int) clutter_actor_get_height (plugin->screensaver_overlay);
  wallpaper = ooze_wallpaper_content (plugin,
                                      plugin->screensaver_overlay,
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
    {
      ooze_screensaver_refresh_wallpaper (plugin);
      if (plugin->screensaver_active)
        ooze_screensaver_flow_render (plugin, TRUE);
    }
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
  if (!plugin->screensaver_active)
    {
      if (plugin->screensaver_overlay)
        clutter_actor_hide (plugin->screensaver_overlay);
      if (plugin->screensaver_curtain)
        {
          clutter_actor_remove_all_transitions (plugin->screensaver_curtain);
          clutter_actor_set_opacity (plugin->screensaver_curtain, 0);
          clutter_actor_hide (plugin->screensaver_curtain);
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
  if (!ooze_screensaver_flow_enabled (plugin))
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

/* --- Phased black curtain --------------------------------------------
 *
 * Activation and dismissal run through an opaque black actor on top of the
 * saver overlay so the user sees three distinct phases:
 *   1. desktop fades to black (curtain 0 -> 255)
 *   2. the running saver is revealed (curtain 255 -> 0)
 *   3. on stop, saver fades to black then back to the desktop.
 */

static void
ooze_screensaver_curtain_ensure (OozePlugin *plugin)
{
  ClutterActor *stage;
  CoglColor black;
  int width;
  int height;

  if (plugin->screensaver_curtain)
    return;

  stage = ooze_screensaver_get_stage (plugin);
  if (!stage)
    return;

  cogl_color_init_from_4f (&black, 0.0f, 0.0f, 0.0f, 1.0f);

  width = (int) clutter_actor_get_width (stage);
  height = (int) clutter_actor_get_height (stage);

  plugin->screensaver_curtain = clutter_actor_new ();
  clutter_actor_set_background_color (plugin->screensaver_curtain, &black);
  clutter_actor_set_size (plugin->screensaver_curtain,
                          (gfloat) width, (gfloat) height);
  clutter_actor_set_position (plugin->screensaver_curtain, 0.0f, 0.0f);
  clutter_actor_set_reactive (plugin->screensaver_curtain, FALSE);
  clutter_actor_set_opacity (plugin->screensaver_curtain, 0);
  clutter_actor_hide (plugin->screensaver_curtain);
  clutter_actor_add_child (stage, plugin->screensaver_curtain);
}

static void
ooze_screensaver_curtain_fade (OozePlugin *plugin,
                               guint       from,
                               guint       to)
{
  ClutterTransition *fade;
  ClutterActor *stage = ooze_screensaver_get_stage (plugin);

  ooze_screensaver_curtain_ensure (plugin);
  if (!plugin->screensaver_curtain)
    return;

  if (stage)
    clutter_actor_set_child_above_sibling (stage,
                                           plugin->screensaver_curtain,
                                           NULL);
  clutter_actor_remove_all_transitions (plugin->screensaver_curtain);
  clutter_actor_show (plugin->screensaver_curtain);
  clutter_actor_set_opacity (plugin->screensaver_curtain, from);

  fade = clutter_property_transition_new ("opacity");
  clutter_transition_set_from (fade, G_TYPE_UINT, from);
  clutter_transition_set_to (fade, G_TYPE_UINT, to);
  clutter_timeline_set_duration (CLUTTER_TIMELINE (fade),
                                 OOZE_SAVER_PHASE_MS);
  clutter_timeline_set_progress_mode (CLUTTER_TIMELINE (fade),
                                      CLUTTER_EASE_IN_OUT_SINE);
  clutter_actor_add_transition (plugin->screensaver_curtain,
                                "curtain-fade", fade);
  g_object_unref (fade);
}

/* While the opaque curtain fully obscures the desktop, Mutter stops
 * painting the windows behind it. Force them to repaint as the curtain
 * lifts so apps fade back in with the curtain instead of popping in once
 * it is gone. */
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

static gboolean
ooze_screensaver_reveal_saver (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_phase_id = 0;
  if (plugin->screensaver_active)
    ooze_screensaver_curtain_fade (plugin, 255u, 0u);
  return G_SOURCE_REMOVE;
}

static gboolean
ooze_screensaver_finish_dismiss (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->screensaver_phase_id = 0;

  /* Screen is black now: tear the saver down behind the curtain, then
   * lift the curtain to reveal the desktop. */
  ooze_screensaver_mode.stop (plugin);
  if (plugin->screensaver_timeline)
    clutter_timeline_pause (plugin->screensaver_timeline);
  if (plugin->screensaver_overlay)
    {
      clutter_actor_remove_all_transitions (plugin->screensaver_overlay);
      clutter_actor_hide (plugin->screensaver_overlay);
      clutter_actor_set_opacity (plugin->screensaver_overlay, 0);
    }

  ooze_screensaver_repaint_desktop (plugin);
  ooze_screensaver_curtain_fade (plugin, 255u, 0u);
  if (plugin->screensaver_fade_id)
    g_source_remove (plugin->screensaver_fade_id);
  plugin->screensaver_fade_id =
    g_timeout_add (OOZE_SAVER_PHASE_MS + 40,
                   ooze_screensaver_hide_after_fade,
                   plugin);
  return G_SOURCE_REMOVE;
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
      !ooze_screensaver_flow_enabled (plugin) ||
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
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
    }

  /* The saver renders at full opacity underneath the black curtain; the
   * curtain drives the desktop -> black -> saver phases on top. */
  clutter_actor_remove_all_transitions (plugin->screensaver_overlay);
  clutter_actor_show (plugin->screensaver_overlay);
  clutter_actor_set_opacity (plugin->screensaver_overlay, 255);

  ooze_screensaver_mode.start (plugin);
  clutter_timeline_rewind (plugin->screensaver_timeline);
  clutter_timeline_start (plugin->screensaver_timeline);

  /* Phase 1: fade the desktop to black; phase 2 reveals the saver. */
  ooze_screensaver_curtain_fade (plugin, 0u, 255u);
  plugin->screensaver_phase_id =
    g_timeout_add (OOZE_SAVER_PHASE_MS + 20,
                   ooze_screensaver_reveal_saver,
                   plugin);

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_lock_backdrop (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (plugin->shutting_down || !ooze_screensaver_flow_enabled (plugin))
    return;

  ooze_screensaver_build (plugin);
  if (!plugin->screensaver_overlay)
    return;

  if (plugin->screensaver_fade_id)
    {
      g_source_remove (plugin->screensaver_fade_id);
      plugin->screensaver_fade_id = 0;
    }

  if (!plugin->screensaver_active)
    {
      plugin->screensaver_active = TRUE;
      plugin->screensaver_input_armed = FALSE;
      clutter_actor_remove_all_transitions (plugin->screensaver_overlay);
      clutter_actor_show (plugin->screensaver_overlay);
      clutter_actor_set_opacity (plugin->screensaver_overlay, 255);
      ooze_screensaver_mode.start (plugin);
    }
  else
    {
      plugin->screensaver_input_armed = FALSE;
    }

  if (plugin->screensaver_timeline &&
      !clutter_timeline_is_playing (plugin->screensaver_timeline))
    clutter_timeline_start (plugin->screensaver_timeline);

  ooze_screensaver_sync_watches (plugin);
}

void
ooze_screensaver_unlock_backdrop (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;

  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!ooze_screensaver_flow_enabled (plugin))
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
    {
      if (plugin->screensaver_timeline &&
          !clutter_timeline_is_playing (plugin->screensaver_timeline))
        clutter_timeline_start (plugin->screensaver_timeline);
      ooze_screensaver_rearm (plugin);
    }
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
  if (plugin->screensaver_phase_id)
    {
      g_source_remove (plugin->screensaver_phase_id);
      plugin->screensaver_phase_id = 0;
    }

  /* Phase 3: fade the running saver to black, then finish_dismiss tears
   * it down behind the curtain and lifts the curtain to the desktop. */
  ooze_screensaver_curtain_fade (plugin, 0u, 255u);
  plugin->screensaver_phase_id =
    g_timeout_add (OOZE_SAVER_PHASE_MS + 20,
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
ooze_screensaver_mode_changed (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (!ooze_screensaver_flow_enabled (plugin) &&
      plugin->screensaver_active)
    {
      ooze_screensaver_dismiss (plugin);
      return;
    }

  /* Swap the scene live if the saver is already on screen. */
  if (plugin->screensaver_active && plugin->screensaver_flow_actor)
    {
      OozeFlowState *flow = ooze_screensaver_get_flow (plugin);
      g_autofree char *scene = ooze_screensaver_current_scene (plugin);

      if (!flow || g_strcmp0 (flow->scene, scene) != 0)
        {
          ooze_screensaver_mode_resize (
            plugin,
            (int) clutter_actor_get_width (plugin->screensaver_overlay),
            (int) clutter_actor_get_height (plugin->screensaver_overlay));
          flow = ooze_screensaver_get_flow (plugin);
          if (flow)
            {
              flow->phase_start_us = g_get_monotonic_time ();
              ooze_screensaver_flow_render (plugin, TRUE);
            }
          ooze_screensaver_hack_start (plugin, scene);
        }
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

  ooze_screensaver_mode.stop (plugin);
  ooze_screensaver_hack_cleanup (plugin, TRUE);

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
  g_clear_pointer (&plugin->screensaver_curtain, clutter_actor_destroy);
  plugin->screensaver_flow_actor = NULL;
  plugin->screensaver_active = FALSE;
  plugin->screensaver_input_armed = FALSE;
}
