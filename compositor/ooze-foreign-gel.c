#include "ooze-foreign-gel.h"

#include "ooze-aqua-draw.h"

#include "../common/aqua-chrome.h"

#include <meta/display.h>
#include <meta/meta-plugin.h>
#include <meta/meta-window-actor.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#include <clutter/clutter.h>

/*
 * Nominal foreign title-bar geometry. libadwaita's default AdwHeaderBar is
 * ~46px tall; the lights sit vertically centred in that band, inset from the
 * left by the standard Aqua margin. These are the tunables to nudge on a real
 * display if the overlay sits slightly off.
 */
#define OOZE_FOREIGN_BAR_HEIGHT 46.0f
#define OOZE_FOREIGN_LIGHT_HITBOX \
  (AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP)

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
  gulong        position_id;
  gulong        size_id;
  gulong        unmanaged_id;
} OozeForeignGel;

struct _OozeForeignGelState
{
  MetaDisplay *display;
  GHashTable  *overlays; /* MetaWindow* -> OozeForeignGel* */
  gulong       window_created_id;
};

typedef struct _OozeForeignGelState OozeForeignGelState;

gboolean
ooze_foreign_gel_enabled (void)
{
  const char *v = g_getenv ("OOZE_FOREIGN_GEL");

  if (!v || !*v)
    return FALSE;
  return g_ascii_strcasecmp (v, "1") == 0 ||
         g_ascii_strcasecmp (v, "true") == 0 ||
         g_ascii_strcasecmp (v, "yes") == 0 ||
         g_ascii_strcasecmp (v, "on") == 0;
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */

static void
ooze_foreign_gel_add_light (ClutterActor *parent,
                            int           index,
                            gfloat        r,
                            gfloat        g,
                            gfloat        b)
{
  ClutterActor *light = clutter_actor_new ();
  ClutterContent *content;
  gfloat step = AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  gfloat y = (OOZE_FOREIGN_BAR_HEIGHT - AQUA_TRAFFIC_LIGHT_SIZE) / 2.0f;

  clutter_actor_set_size (light, AQUA_TRAFFIC_LIGHT_SIZE,
                          AQUA_TRAFFIC_LIGHT_SIZE);
  clutter_actor_set_position (light,
                              AQUA_TRAFFIC_LIGHT_MARGIN + index * step, y);
  clutter_actor_set_reactive (light, FALSE);

  content = ooze_aqua_traffic_light_content (light, AQUA_TRAFFIC_LIGHT_SIZE,
                                             r, g, b);
  if (content)
    clutter_actor_set_content (light, content);

  clutter_actor_add_child (parent, light);
}

static void
ooze_foreign_gel_build_lights (ClutterActor *parent)
{
  ooze_foreign_gel_add_light (parent, 0,
                              AQUA_TRAFFIC_CLOSE_R, AQUA_TRAFFIC_CLOSE_G,
                              AQUA_TRAFFIC_CLOSE_B);
  ooze_foreign_gel_add_light (parent, 1,
                              AQUA_TRAFFIC_MINIMIZE_R, AQUA_TRAFFIC_MINIMIZE_G,
                              AQUA_TRAFFIC_MINIMIZE_B);
  ooze_foreign_gel_add_light (parent, 2,
                              AQUA_TRAFFIC_ZOOM_R, AQUA_TRAFFIC_ZOOM_G,
                              AQUA_TRAFFIC_ZOOM_B);
}

static OozeForeignHit
ooze_foreign_gel_hit (gfloat x)
{
  gfloat step = AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  gfloat local = x - AQUA_TRAFFIC_LIGHT_MARGIN;

  if (local < 0.0f)
    return OOZE_FOREIGN_HIT_NONE;

  if (local < step)
    return OOZE_FOREIGN_HIT_CLOSE;
  if (local < 2 * step)
    return OOZE_FOREIGN_HIT_MINIMIZE;
  if (local < 3 * step)
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
ooze_foreign_gel_on_button (ClutterActor *actor G_GNUC_UNUSED,
                            ClutterEvent *event,
                            gpointer      user_data)
{
  OozeForeignGel *gel = user_data;
  gfloat x = 0.0f;
  gfloat y = 0.0f;

  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
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

  return TRUE;
}

static void ooze_foreign_gel_remove (OozeForeignGelState *state,
                                     MetaWindow          *window);

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

  g_clear_signal_handler (&gel->position_id, gel->window);
  g_clear_signal_handler (&gel->size_id, gel->window);
  g_clear_signal_handler (&gel->unmanaged_id, gel->window);
  g_clear_pointer (&gel->actor, clutter_actor_destroy);
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

  width = AQUA_TRAFFIC_LIGHT_MARGIN * 2 +
          3 * AQUA_TRAFFIC_LIGHT_SIZE + 2 * AQUA_TRAFFIC_LIGHT_GAP;

  gel = g_new0 (OozeForeignGel, 1);
  gel->plugin = plugin;
  gel->window = window;

  gel->actor = clutter_actor_new ();
  clutter_actor_set_size (gel->actor, width, OOZE_FOREIGN_BAR_HEIGHT);
  clutter_actor_set_reactive (gel->actor, TRUE);
  ooze_foreign_gel_build_lights (gel->actor);

  g_signal_connect (gel->actor, "button-press-event",
                    G_CALLBACK (ooze_foreign_gel_on_button), gel);

  clutter_actor_add_child (CLUTTER_ACTOR (window_actor), gel->actor);
  clutter_actor_set_child_above_sibling (CLUTTER_ACTOR (window_actor),
                                         gel->actor, NULL);

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
}

static void
ooze_foreign_gel_on_window_created (MetaDisplay *display G_GNUC_UNUSED,
                                    MetaWindow  *window,
                                    gpointer     user_data)
{
  OozePlugin *plugin = user_data;

  ooze_foreign_gel_attach (plugin->foreign_gel, plugin, window);
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
  state->window_created_id =
    g_signal_connect (display, "window-created",
                      G_CALLBACK (ooze_foreign_gel_on_window_created), plugin);
  plugin->foreign_gel = state;

  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    ooze_foreign_gel_attach (state, plugin, l->data);
  g_list_free (windows);

  g_print ("Ooze: foreign Gel traffic lights enabled\n");
}

void
ooze_foreign_gel_shutdown (OozePlugin *plugin)
{
  OozeForeignGelState *state = plugin->foreign_gel;

  if (!state)
    return;

  g_clear_signal_handler (&state->window_created_id, state->display);
  g_hash_table_destroy (state->overlays);
  g_free (state);
  plugin->foreign_gel = NULL;
}
