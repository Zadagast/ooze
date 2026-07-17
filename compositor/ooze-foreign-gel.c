#include "ooze-foreign-gel.h"

#include "ooze-aqua-draw.h"
#include "ooze-theme.h"

#include "../common/aqua-chrome.h"

#include <meta/display.h>
#include <meta/meta-plugin.h>
#include <meta/meta-window-actor.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#include <clutter/clutter.h>

#include <string.h>

/*
 * Foreign title-bar geometry, measured against libadwaita's default
 * AdwHeaderBar: 24px window-control buttons on a 37px pitch, first button
 * centred 22px from the frame's top-left corner. A header-coloured pill is
 * painted over the client's own buttons (the overlay is reactive, so the
 * client never sees hover/press there), then the lights are drawn on top at
 * the same size as first-party Ooze Gel controls.
 *
 * Tunable per run for alignment iteration:
 *   OOZE_FOREIGN_GEL_SIZE  light diameter        (default 14, Ooze size)
 *   OOZE_FOREIGN_GEL_PITCH centre-to-centre step (default 37)
 *   OOZE_FOREIGN_GEL_X     first light centre x  (default 22)
 *   OOZE_FOREIGN_GEL_Y     light centre y        (default 22)
 */
#define OOZE_FOREIGN_LIGHT_SIZE  ((gfloat) AQUA_TRAFFIC_LIGHT_SIZE)
#define OOZE_FOREIGN_LIGHT_PITCH 37.0f
#define OOZE_FOREIGN_LIGHT_X     22.0f
#define OOZE_FOREIGN_LIGHT_Y     22.0f

/* The covered client button diameter (24px) plus a little bleed. */
#define OOZE_FOREIGN_COVER_EXTENT 16.0f

static gfloat
ooze_foreign_gel_metric (const char *env, gfloat fallback)
{
  const char *v = g_getenv (env);
  char *end = NULL;
  double parsed;

  if (!v || !*v)
    return fallback;

  parsed = g_ascii_strtod (v, &end);
  if (end == v || parsed <= 0.0 || parsed > 500.0)
    return fallback;

  return (gfloat) parsed;
}

static gfloat
ooze_foreign_gel_size (void)
{
  return ooze_foreign_gel_metric ("OOZE_FOREIGN_GEL_SIZE",
                                  OOZE_FOREIGN_LIGHT_SIZE);
}

static gfloat
ooze_foreign_gel_pitch (void)
{
  return ooze_foreign_gel_metric ("OOZE_FOREIGN_GEL_PITCH",
                                  OOZE_FOREIGN_LIGHT_PITCH);
}

static gfloat
ooze_foreign_gel_first_x (void)
{
  return ooze_foreign_gel_metric ("OOZE_FOREIGN_GEL_X",
                                  OOZE_FOREIGN_LIGHT_X);
}

static gfloat
ooze_foreign_gel_centre_y (void)
{
  return ooze_foreign_gel_metric ("OOZE_FOREIGN_GEL_Y",
                                  OOZE_FOREIGN_LIGHT_Y);
}

typedef enum
{
  OOZE_FOREIGN_HIT_NONE,
  OOZE_FOREIGN_HIT_CLOSE,
  OOZE_FOREIGN_HIT_MINIMIZE,
  OOZE_FOREIGN_HIT_ZOOM,
} OozeForeignHit;

typedef struct
{
  OozePlugin   *plugin;
  MetaWindow   *window;
  ClutterActor *actor;
  ClutterActor *lights[3];
  gulong        position_id;
  gulong        size_id;
  gulong        unmanaged_id;
  gulong        actor_destroy_id;
  gulong        enter_id;
  gulong        leave_id;
  gboolean      hovered;
  gboolean      theme_watched;
} OozeForeignGel;

struct _OozeForeignGelState
{
  MetaDisplay *display;
  GHashTable  *overlays; /* MetaWindow* -> OozeForeignGel* */
  GHashTable  *retries;  /* MetaWindow* -> OozeForeignGelRetry* */
};

typedef struct _OozeForeignGelState OozeForeignGelState;

typedef struct
{
  OozeForeignGelState *state;
  OozePlugin           *plugin;
  MetaWindow           *window;
  gulong                window_type_id;
  gulong                wm_class_id;
  gulong                gtk_app_id_id;
  gulong                unmanaged_id;
} OozeForeignGelRetry;

gboolean
ooze_foreign_gel_enabled (void)
{
  const char *v = g_getenv ("OOZE_FOREIGN_GEL");

  /* On by default; opt out with OOZE_FOREIGN_GEL=0/false/no/off. */
  if (!v || !*v)
    return TRUE;
  return !(g_ascii_strcasecmp (v, "0") == 0 ||
           g_ascii_strcasecmp (v, "false") == 0 ||
           g_ascii_strcasecmp (v, "no") == 0 ||
           g_ascii_strcasecmp (v, "off") == 0);
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */

static ClutterContent *
ooze_foreign_gel_pill_content (ClutterActor *ref_actor,
                               int           width,
                               int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  gboolean dark = ooze_theme_is_dark (NULL);
  double radius = height / 2.0;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  /* Rounded pill matching the Adwaita header-bar background so it reads as
   * part of the client's own title bar. */
  cairo_new_path (cr);
  cairo_arc (cr, radius, radius, radius, G_PI / 2.0, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - radius, radius, radius,
             3.0 * G_PI / 2.0, G_PI / 2.0);
  cairo_close_path (cr);

  if (dark)
    cairo_set_source_rgb (cr, 0.157, 0.157, 0.173);
  else
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = ooze_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static void
ooze_foreign_gel_add_pill (ClutterActor *parent)
{
  ClutterActor *pill = clutter_actor_new ();
  g_autoptr (ClutterContent) content = NULL;
  gfloat cy = ooze_foreign_gel_centre_y ();
  gfloat x0 = ooze_foreign_gel_first_x () - OOZE_FOREIGN_COVER_EXTENT;
  gfloat x1 = ooze_foreign_gel_first_x () + 2.0f * ooze_foreign_gel_pitch () +
              OOZE_FOREIGN_COVER_EXTENT;
  gfloat height = 2.0f * OOZE_FOREIGN_COVER_EXTENT;

  if (x0 < 0.0f)
    x0 = 0.0f;

  clutter_actor_set_size (pill, x1 - x0, height);
  clutter_actor_set_position (pill, x0, cy - height / 2.0f);
  clutter_actor_set_reactive (pill, FALSE);
  clutter_actor_add_child (parent, pill);

  content = ooze_foreign_gel_pill_content (pill, (int) (x1 - x0),
                                           (int) height);
  if (content)
    ooze_aqua_actor_set_content (pill, g_steal_pointer (&content),
                                 (int) (x1 - x0), (int) height);
  else
    g_warning ("Ooze: foreign Gel pill has no content (no Cogl context)");
}

static void
ooze_foreign_gel_set_light_content (OozeForeignGel *gel,
                                    int             index,
                                    gfloat          r,
                                    gfloat          g,
                                    gfloat          b,
                                    ClutterActor   *light)
{
  gfloat size = ooze_foreign_gel_size ();
  g_autoptr (ClutterContent) content = NULL;

  content = ooze_aqua_traffic_light_content (
    light, (int) size, r, g, b, gel->hovered,
    index == 0 ? AQUA_TRAFFIC_GLYPH_CLOSE :
    index == 1 ? AQUA_TRAFFIC_GLYPH_MINIMIZE :
                 AQUA_TRAFFIC_GLYPH_ZOOM);
  if (content)
    ooze_aqua_actor_set_content (light, g_steal_pointer (&content),
                                 (int) size, (int) size);
  else
    g_warning ("Ooze: foreign Gel light %d has no content (no Cogl context)",
               index);
}

static void
ooze_foreign_gel_add_light (OozeForeignGel *gel,
                            int             index,
                            gfloat          r,
                            gfloat          g,
                            gfloat          b)
{
  ClutterActor *light = clutter_actor_new ();
  gfloat size = ooze_foreign_gel_size ();
  gfloat cx = ooze_foreign_gel_first_x () + index * ooze_foreign_gel_pitch ();
  gfloat cy = ooze_foreign_gel_centre_y ();

  clutter_actor_set_size (light, size, size);
  clutter_actor_set_position (light, cx - size / 2.0f, cy - size / 2.0f);
  clutter_actor_set_reactive (light, FALSE);
  clutter_actor_add_child (gel->actor, light);
  gel->lights[index] = light;
  ooze_foreign_gel_set_light_content (gel, index, r, g, b, light);
}

static void
ooze_foreign_gel_build_lights (OozeForeignGel *gel)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (gel->lights); i++)
    gel->lights[i] = NULL;

  ooze_foreign_gel_add_pill (gel->actor);

  ooze_foreign_gel_add_light (gel, 0,
                              AQUA_TRAFFIC_CLOSE_R, AQUA_TRAFFIC_CLOSE_G,
                              AQUA_TRAFFIC_CLOSE_B);
  ooze_foreign_gel_add_light (gel, 1,
                              AQUA_TRAFFIC_MINIMIZE_R, AQUA_TRAFFIC_MINIMIZE_G,
                              AQUA_TRAFFIC_MINIMIZE_B);
  ooze_foreign_gel_add_light (gel, 2,
                              AQUA_TRAFFIC_ZOOM_R, AQUA_TRAFFIC_ZOOM_G,
                              AQUA_TRAFFIC_ZOOM_B);
}

static void
ooze_foreign_gel_update_lights (OozeForeignGel *gel)
{
  static const gfloat colors[3][3] = {
    { AQUA_TRAFFIC_CLOSE_R,    AQUA_TRAFFIC_CLOSE_G,    AQUA_TRAFFIC_CLOSE_B },
    { AQUA_TRAFFIC_MINIMIZE_R, AQUA_TRAFFIC_MINIMIZE_G, AQUA_TRAFFIC_MINIMIZE_B },
    { AQUA_TRAFFIC_ZOOM_R,     AQUA_TRAFFIC_ZOOM_G,     AQUA_TRAFFIC_ZOOM_B },
  };
  guint i;

  if (!gel || !gel->actor)
    return;

  for (i = 0; i < G_N_ELEMENTS (gel->lights); i++)
    {
      if (!gel->lights[i])
        continue;

      ooze_foreign_gel_set_light_content (gel, i,
                                          colors[i][0],
                                          colors[i][1],
                                          colors[i][2],
                                          gel->lights[i]);
    }
}

static OozeForeignHit
ooze_foreign_gel_hit (gfloat x)
{
  gfloat pitch = ooze_foreign_gel_pitch ();
  gfloat local = x - (ooze_foreign_gel_first_x () - pitch / 2.0f);

  if (local < 0.0f)
    return OOZE_FOREIGN_HIT_NONE;

  if (local < pitch)
    return OOZE_FOREIGN_HIT_CLOSE;
  if (local < 2 * pitch)
    return OOZE_FOREIGN_HIT_MINIMIZE;
  if (local < 3 * pitch)
    return OOZE_FOREIGN_HIT_ZOOM;

  return OOZE_FOREIGN_HIT_NONE;
}

/* ── Positioning ─────────────────────────────────────────────────────────── */

static void
ooze_foreign_gel_reposition (OozeForeignGel *gel)
{
  MtkRectangle frame;
  MtkRectangle buffer;

  meta_window_get_frame_rect (gel->window, &frame);
  meta_window_get_buffer_rect (gel->window, &buffer);

  /* The actor is parented to the window actor, whose origin is the buffer
   * top-left (client-drawn shadows extend outside the visible frame). Offset
   * the overlay to the visible frame's top-left. */
  clutter_actor_set_position (gel->actor,
                              (gfloat) (frame.x - buffer.x),
                              (gfloat) (frame.y - buffer.y));
}

static void
ooze_foreign_gel_on_geometry_changed (MetaWindow *window G_GNUC_UNUSED,
                                      gpointer    user_data)
{
  ooze_foreign_gel_reposition (user_data);
}

/* ── Input ───────────────────────────────────────────────────────────────── */

static gboolean
ooze_foreign_gel_on_button (ClutterActor *actor,
                            ClutterEvent *event,
                            gpointer      user_data)
{
  OozeForeignGel *gel = user_data;
  gfloat x = 0.0f;
  gfloat y = 0.0f;

  if (!gel || gel->actor != actor || !gel->window ||
      clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  clutter_event_get_coords (event, &x, &y);
  clutter_actor_transform_stage_point (gel->actor, x, y, &x, &y);

  switch (ooze_foreign_gel_hit (x))
    {
    case OOZE_FOREIGN_HIT_CLOSE:
      meta_window_delete (gel->window, clutter_event_get_time (event));
      return CLUTTER_EVENT_STOP;

    case OOZE_FOREIGN_HIT_MINIMIZE:
      meta_window_minimize (gel->window);
      return CLUTTER_EVENT_STOP;

    case OOZE_FOREIGN_HIT_ZOOM:
      if (meta_window_is_fullscreen (gel->window))
        meta_window_unmake_fullscreen (gel->window);
      else if (meta_window_is_maximized (gel->window))
        meta_window_unmaximize (gel->window);
      else
        meta_window_maximize (gel->window);
      return CLUTTER_EVENT_STOP;

    case OOZE_FOREIGN_HIT_NONE:
    default:
      return CLUTTER_EVENT_PROPAGATE;
  }
}

static gboolean
ooze_foreign_gel_on_enter (ClutterActor *actor,
                           ClutterEvent *event G_GNUC_UNUSED,
                           gpointer      user_data)
{
  OozeForeignGel *gel = user_data;

  if (!gel || gel->actor != actor || gel->hovered)
    return CLUTTER_EVENT_PROPAGATE;

  gel->hovered = TRUE;
  ooze_foreign_gel_update_lights (gel);

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_foreign_gel_on_leave (ClutterActor *actor,
                           ClutterEvent *event G_GNUC_UNUSED,
                           gpointer      user_data)
{
  OozeForeignGel *gel = user_data;

  if (!gel || gel->actor != actor || !gel->hovered)
    return CLUTTER_EVENT_PROPAGATE;

  gel->hovered = FALSE;
  ooze_foreign_gel_update_lights (gel);

  return CLUTTER_EVENT_PROPAGATE;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static gboolean
ooze_foreign_gel_is_ooze_app (MetaWindow *window)
{
  const char *app_id = meta_window_get_gtk_application_id (window);
  const char *wm_class;

  if (app_id && g_str_has_prefix (app_id, "org.ooze"))
    return TRUE;

  wm_class = meta_window_get_wm_class (window);
  if (wm_class && g_str_has_prefix (wm_class, "org.ooze"))
    return TRUE;

  return FALSE;
}

/* Apps that hard-code their own window buttons inside the client UI
 * (Flutter/Electron custom chrome). Overlaying Gel on these just adds a
 * second, mismatched set of controls. Matched case-insensitively against
 * both the app-id and the WM_CLASS. */
static const char *const ooze_foreign_gel_builtin_excludes[] = {
  "rustdesk",
  NULL
};

static GPtrArray *
ooze_foreign_gel_get_excludes (void)
{
  static GPtrArray *excludes;

  if (!excludes)
    {
      g_autofree char *path = NULL;
      g_autofree char *data = NULL;
      gsize i;

      excludes = g_ptr_array_new_with_free_func (g_free);
      for (i = 0; ooze_foreign_gel_builtin_excludes[i]; i++)
        g_ptr_array_add (excludes,
                         g_strdup (ooze_foreign_gel_builtin_excludes[i]));

      /* User-extendable list: one app-id or WM_CLASS per line. */
      path = g_build_filename (g_get_user_config_dir (), "ooze",
                               "foreign-gel-exclude.conf", NULL);
      if (g_file_get_contents (path, &data, NULL, NULL))
        {
          g_auto (GStrv) lines = g_strsplit (data, "\n", -1);

          for (i = 0; lines[i]; i++)
            {
              char *line = g_strstrip (lines[i]);

              if (*line == '\0' || *line == '#')
                continue;
              g_ptr_array_add (excludes, g_utf8_strdown (line, -1));
            }
        }
    }

  return excludes;
}

static gboolean
ooze_foreign_gel_is_excluded (MetaWindow *window)
{
  const char *app_id = meta_window_get_gtk_application_id (window);
  const char *wm_class = meta_window_get_wm_class (window);
  g_autofree char *app_id_down = NULL;
  g_autofree char *wm_class_down = NULL;
  GPtrArray *excludes = ooze_foreign_gel_get_excludes ();
  guint i;

  if (app_id)
    app_id_down = g_utf8_strdown (app_id, -1);
  if (wm_class)
    wm_class_down = g_utf8_strdown (wm_class, -1);

  for (i = 0; i < excludes->len; i++)
    {
      const char *entry = excludes->pdata[i];

      if ((app_id_down && strstr (app_id_down, entry)) ||
          (wm_class_down && strstr (wm_class_down, entry)))
        return TRUE;
    }

  return FALSE;
}

static gboolean
ooze_foreign_gel_should_attach (MetaWindow *window)
{
  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return FALSE;

  /* X11 windows get real Mutter frames; only decorate Wayland CSD clients. */
  if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_WAYLAND)
    return FALSE;

  if (ooze_foreign_gel_is_ooze_app (window))
    return FALSE;

  if (ooze_foreign_gel_is_excluded (window))
    return FALSE;

  return TRUE;
}

static void ooze_foreign_gel_remove (OozeForeignGelState *state,
                                     MetaWindow          *window);
static void ooze_foreign_gel_attach (OozeForeignGelState *state,
                                     OozePlugin          *plugin,
                                     MetaWindow          *window);
static void ooze_foreign_gel_build_lights (OozeForeignGel *gel);

static void
ooze_foreign_gel_retry_free (gpointer data)
{
  OozeForeignGelRetry *retry = data;

  g_clear_signal_handler (&retry->window_type_id, retry->window);
  g_clear_signal_handler (&retry->wm_class_id, retry->window);
  g_clear_signal_handler (&retry->gtk_app_id_id, retry->window);
  g_clear_signal_handler (&retry->unmanaged_id, retry->window);
  g_free (retry);
}

static void
ooze_foreign_gel_retry_remove (OozeForeignGelState *state,
                               MetaWindow          *window)
{
  if (!state)
    return;

  g_hash_table_remove (state->retries, window);
}

static void
ooze_foreign_gel_on_retry_unmanaged (MetaWindow *window,
                                     gpointer    user_data)
{
  OozeForeignGelRetry *retry = user_data;

  ooze_foreign_gel_retry_remove (retry->state, window);
}

static void
ooze_foreign_gel_on_retry_state_changed (MetaWindow *window,
                                         GParamSpec *pspec G_GNUC_UNUSED,
                                         gpointer    user_data)
{
  OozeForeignGelRetry *retry = user_data;
  OozeForeignGelState *state = retry->state;

  if (g_hash_table_contains (state->overlays, window))
    {
      ooze_foreign_gel_retry_remove (state, window);
      return;
    }

  if (!ooze_foreign_gel_should_attach (window))
    return;

  ooze_foreign_gel_attach (state, retry->plugin, window);
  if (g_hash_table_contains (state->overlays, window))
    ooze_foreign_gel_retry_remove (state, window);
}

static void
ooze_foreign_gel_watch_for_retry (OozeForeignGelState *state,
                                  OozePlugin          *plugin,
                                  MetaWindow          *window)
{
  OozeForeignGelRetry *retry;

  if (g_hash_table_contains (state->overlays, window) ||
      g_hash_table_contains (state->retries, window))
    return;

  retry = g_new0 (OozeForeignGelRetry, 1);
  retry->state = state;
  retry->plugin = plugin;
  retry->window = window;
  retry->window_type_id =
    g_signal_connect (window, "notify::window-type",
                      G_CALLBACK (ooze_foreign_gel_on_retry_state_changed),
                      retry);
  retry->wm_class_id =
    g_signal_connect (window, "notify::wm-class",
                      G_CALLBACK (ooze_foreign_gel_on_retry_state_changed),
                      retry);
  retry->gtk_app_id_id =
    g_signal_connect (window, "notify::gtk-application-id",
                      G_CALLBACK (ooze_foreign_gel_on_retry_state_changed),
                      retry);
  retry->unmanaged_id =
    g_signal_connect (window, "unmanaged",
                      G_CALLBACK (ooze_foreign_gel_on_retry_unmanaged),
                      retry);
  g_hash_table_insert (state->retries, window, retry);
}

static void
ooze_foreign_gel_on_theme_changed (gpointer user_data)
{
  OozeForeignGel *gel = user_data;

  if (!gel->actor)
    return;

  /* The pill colour follows the header background; rebuild on toggle. */
  for (guint i = 0; i < G_N_ELEMENTS (gel->lights); i++)
    gel->lights[i] = NULL;
  clutter_actor_destroy_all_children (gel->actor);
  ooze_foreign_gel_build_lights (gel);
}

static void
ooze_foreign_gel_on_actor_destroy (ClutterActor *actor G_GNUC_UNUSED,
                                   gpointer      user_data)
{
  OozeForeignGel *gel = user_data;

  gel->actor = NULL;
  gel->actor_destroy_id = 0;
  gel->enter_id = 0;
  gel->leave_id = 0;
  gel->hovered = FALSE;
  for (guint i = 0; i < G_N_ELEMENTS (gel->lights); i++)
    gel->lights[i] = NULL;
}

static void
ooze_foreign_gel_on_unmanaged (MetaWindow *window,
                               gpointer    user_data)
{
  OozeForeignGel *gel = user_data;

  ooze_foreign_gel_remove (gel->plugin->foreign_gel, window);
}

static void
ooze_foreign_gel_free (gpointer data)
{
  OozeForeignGel *gel = data;

  if (gel->theme_watched)
    ooze_theme_unwatch (ooze_theme_get_default (),
                        ooze_foreign_gel_on_theme_changed, gel);
  g_clear_signal_handler (&gel->position_id, gel->window);
  g_clear_signal_handler (&gel->size_id, gel->window);
  g_clear_signal_handler (&gel->unmanaged_id, gel->window);
  if (gel->actor)
    {
      g_clear_signal_handler (&gel->enter_id, gel->actor);
      g_clear_signal_handler (&gel->leave_id, gel->actor);
      g_clear_signal_handler (&gel->actor_destroy_id, gel->actor);
      for (guint i = 0; i < G_N_ELEMENTS (gel->lights); i++)
        gel->lights[i] = NULL;
      clutter_actor_destroy (gel->actor);
      gel->actor = NULL;
    }
  g_free (gel);
}

static void
ooze_foreign_gel_remove (OozeForeignGelState *state,
                         MetaWindow          *window)
{
  if (!state)
    return;

  g_hash_table_remove (state->overlays, window);
}

static void
ooze_foreign_gel_attach (OozeForeignGelState *state,
                         OozePlugin          *plugin,
                         MetaWindow          *window)
{
  OozeForeignGel *gel;
  MetaWindowActor *window_actor;
  gfloat width;

  if (g_hash_table_contains (state->overlays, window))
    return;
  if (!ooze_foreign_gel_should_attach (window))
    return;

  window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!window_actor)
    return;

  /* Symmetric band around the three light centres. */
  width = 2.0f * ooze_foreign_gel_first_x () + 2.0f * ooze_foreign_gel_pitch ();

  gel = g_new0 (OozeForeignGel, 1);
  gel->plugin = plugin;
  gel->window = window;

  gel->actor = clutter_actor_new ();
  clutter_actor_set_size (gel->actor, width,
                          2.0f * ooze_foreign_gel_centre_y ());
  clutter_actor_set_reactive (gel->actor, TRUE);

  g_signal_connect (gel->actor, "button-press-event",
                    G_CALLBACK (ooze_foreign_gel_on_button), gel);
  gel->enter_id =
    g_signal_connect (gel->actor, "enter-event",
                      G_CALLBACK (ooze_foreign_gel_on_enter), gel);
  gel->leave_id =
    g_signal_connect (gel->actor, "leave-event",
                      G_CALLBACK (ooze_foreign_gel_on_leave), gel);
  gel->actor_destroy_id =
    g_signal_connect (gel->actor, "destroy",
                      G_CALLBACK (ooze_foreign_gel_on_actor_destroy), gel);

  clutter_actor_add_child (CLUTTER_ACTOR (window_actor), gel->actor);
  clutter_actor_set_child_above_sibling (CLUTTER_ACTOR (window_actor),
                                         gel->actor, NULL);

  /* Build the lights only after the overlay is parented: the texture upload
   * resolves its CoglContext by walking the actor's parent chain, so content
   * created on an unparented actor comes back NULL (invisible lights). */
  ooze_foreign_gel_build_lights (gel);

  ooze_theme_watch (ooze_theme_get_default (),
                    ooze_foreign_gel_on_theme_changed, gel);
  gel->theme_watched = TRUE;

  gel->position_id =
    g_signal_connect (window, "position-changed",
                      G_CALLBACK (ooze_foreign_gel_on_geometry_changed), gel);
  gel->size_id =
    g_signal_connect (window, "size-changed",
                      G_CALLBACK (ooze_foreign_gel_on_geometry_changed), gel);
  gel->unmanaged_id =
    g_signal_connect (window, "unmanaged",
                      G_CALLBACK (ooze_foreign_gel_on_unmanaged), gel);

  g_hash_table_insert (state->overlays, window, gel);
  ooze_foreign_gel_reposition (gel);

  g_message ("Ooze: foreign Gel attached to %s",
             meta_window_get_wm_class (window) ?
               meta_window_get_wm_class (window) :
               meta_window_get_description (window));
}

static void
ooze_foreign_gel_attach_or_watch (OozeForeignGelState *state,
                                   OozePlugin          *plugin,
                                   MetaWindow          *window)
{
  ooze_foreign_gel_attach (state, plugin, window);

  if (!g_hash_table_contains (state->overlays, window) &&
      !ooze_foreign_gel_should_attach (window))
    ooze_foreign_gel_watch_for_retry (state, plugin, window);
}

void
ooze_foreign_gel_maybe_attach (OozePlugin      *plugin,
                               MetaWindowActor *actor)
{
  MetaWindow *window;

  if (!plugin->foreign_gel)
    return;

  window = meta_window_actor_get_meta_window (actor);
  if (!window)
    return;

  ooze_foreign_gel_attach_or_watch (plugin->foreign_gel, plugin, window);
}

void
ooze_foreign_gel_init (OozePlugin *plugin)
{
  OozeForeignGelState *state;
  MetaDisplay *display;
  GList *windows;
  GList *l;

  if (!ooze_foreign_gel_enabled ())
    return;
  if (plugin->foreign_gel)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));

  state = g_new0 (OozeForeignGelState, 1);
  state->display = display;
  state->overlays = g_hash_table_new_full (NULL, NULL, NULL,
                                            ooze_foreign_gel_free);
  state->retries = g_hash_table_new_full (NULL, NULL, NULL,
                                          ooze_foreign_gel_retry_free);
  plugin->foreign_gel = state;

  /* Windows that mapped before init; later ones attach from the plugin's
   * map vfunc (at "window-created" the compositor actor does not exist
   * yet, so attaching there silently fails). */
  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    ooze_foreign_gel_attach_or_watch (state, plugin, l->data);
  g_list_free (windows);

  g_print ("Ooze: foreign Gel traffic lights enabled\n");
}

void
ooze_foreign_gel_shutdown (OozePlugin *plugin)
{
  OozeForeignGelState *state = plugin->foreign_gel;

  if (!state)
    return;

  g_hash_table_destroy (state->retries);
  g_hash_table_destroy (state->overlays);
  g_free (state);
  plugin->foreign_gel = NULL;
}
