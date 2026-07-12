#include "my-window.h"

#include <graphene.h>
#include <meta/compositor.h>
#include <meta/meta-enums.h>
#include <meta/window.h>

#define RESIZE_BORDER    8
#define TITLEBAR_HEIGHT  22

typedef struct
{
  ClutterActor *container;
  ClutterActor *north;
  ClutterActor *south;
  ClutterActor *east;
  ClutterActor *west;
  ClutterActor *north_west;
  ClutterActor *north_east;
  ClutterActor *south_west;
  ClutterActor *south_east;
  ClutterActor *titlebar;
  GWeakRef window_ref;
  gulong position_changed_id;
} MyWindowGrabOverlay;

gboolean
my_window_is_client_decorated (MetaWindow *window)
{
  if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_WAYLAND)
    return TRUE;

  if (meta_window_get_gtk_application_id (window) != NULL)
    return TRUE;

  return FALSE;
}

static void
my_window_event_stage_point (ClutterEvent     *event,
                             graphene_point_t *point)
{
  clutter_event_get_position (event, point);
}

static gboolean
my_window_begin_grab (MetaWindow       *window,
                      ClutterEvent     *event,
                      MetaGrabOp         op)
{
  graphene_point_t pos_hint;

  if (op == META_GRAB_OP_NONE)
    return FALSE;

  my_window_event_stage_point (event, &pos_hint);
  meta_window_focus (window, clutter_event_get_time (event));

  return meta_window_begin_grab_op (window,
                                   op,
                                   NULL,
                                   clutter_event_get_time (event),
                                   &pos_hint);
}

gboolean
my_window_begin_grab_from_event (MetaWindow   *window,
                                 ClutterEvent *event,
                                 MetaGrabOp     op)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return FALSE;

  return my_window_begin_grab (window, event, op);
}

static gboolean
on_grab_overlay_press (ClutterActor *actor,
                       ClutterEvent *event,
                       MetaGrabOp    *op_ptr)
{
  MyWindowGrabOverlay *overlay;
  MetaWindow *window;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  overlay = g_object_get_data (G_OBJECT (actor), "grab-overlay");
  if (!overlay)
    return CLUTTER_EVENT_PROPAGATE;

  window = g_weak_ref_get (&overlay->window_ref);
  if (!window)
    return CLUTTER_EVENT_PROPAGATE;

  if (my_window_begin_grab (window, event, *op_ptr))
    {
      g_object_unref (window);
      return CLUTTER_EVENT_STOP;
    }

  g_object_unref (window);
  return CLUTTER_EVENT_PROPAGATE;
}

static ClutterActor *
my_window_create_grab_actor (MyWindowGrabOverlay *overlay,
                             MetaGrabOp            op)
{
  ClutterActor *actor;
  MetaGrabOp *op_storage;

  actor = clutter_actor_new ();
  clutter_actor_set_reactive (actor, TRUE);
  g_object_set_data (G_OBJECT (actor), "grab-overlay", overlay);

  op_storage = g_new (MetaGrabOp, 1);
  *op_storage = op;
  g_object_set_data_full (G_OBJECT (actor),
                          "grab-op",
                          op_storage,
                          g_free);

  g_signal_connect (actor,
                    "button-press-event",
                    G_CALLBACK (on_grab_overlay_press),
                    op_storage);

  return actor;
}

static void
my_window_destroy_grab_overlay (MyWindowGrabOverlay *overlay)
{
  MetaWindow *window;

  if (!overlay)
    return;

  if (overlay->position_changed_id)
    {
      window = g_weak_ref_get (&overlay->window_ref);
      if (window)
        {
          g_signal_handler_disconnect (window, overlay->position_changed_id);
          g_object_unref (window);
        }
      overlay->position_changed_id = 0;
    }

  g_weak_ref_clear (&overlay->window_ref);
  g_clear_pointer (&overlay->container, clutter_actor_destroy);
  g_free (overlay);
}

static void
my_window_place_grab_actor (ClutterActor *actor,
                            gfloat        x,
                            gfloat        y,
                            gfloat        width,
                            gfloat        height)
{
  if (!CLUTTER_IS_ACTOR (actor))
    return;

  if (width < 1.0f || height < 1.0f)
    {
      clutter_actor_hide (actor);
      return;
    }

  clutter_actor_set_position (actor, x, y);
  clutter_actor_set_size (actor, width, height);
  clutter_actor_show (actor);
}

static MyWindowGrabOverlay *
my_window_get_grab_overlay (MetaWindowActor *actor)
{
  return g_object_get_data (G_OBJECT (actor), "my-window-grab-overlay");
}

void
my_window_cancel_scheduled_sync (MetaWindowActor *actor)
{
  gpointer sync_id = g_object_get_data (G_OBJECT (actor), "my-window-sync-id");

  if (sync_id)
    {
      g_source_remove (GPOINTER_TO_UINT (sync_id));
      g_object_set_data (G_OBJECT (actor), "my-window-sync-id", NULL);
    }
}

static gboolean
my_window_sync_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  g_object_set_data (G_OBJECT (actor), "my-window-sync-id", NULL);
  my_window_sync (actor);
  return G_SOURCE_REMOVE;
  /* g_object_unref via destroy notify in schedule_sync */
}

void
my_window_schedule_sync (MetaWindowActor *actor)
{
  if (g_object_get_data (G_OBJECT (actor), "my-window-sync-id"))
    return;

  g_object_ref (actor);
  g_object_set_data (G_OBJECT (actor),
                     "my-window-sync-id",
                     GUINT_TO_POINTER (g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                        my_window_sync_idle,
                                                        actor,
                                                        g_object_unref)));
}

void
my_window_sync (MetaWindowActor *actor)
{
  MetaWindow *window;
  MyWindowGrabOverlay *overlay;
  MtkRectangle frame;
  gboolean csd;
  gfloat border = RESIZE_BORDER;
  gfloat top_inset;
  gfloat width;
  gfloat height;

  if (meta_window_actor_is_destroyed (actor))
    return;

  window = meta_window_actor_get_meta_window (actor);
  if (!window)
    return;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return;

  meta_window_get_frame_rect (window, &frame);
  if (frame.width <= 0 || frame.height <= 0)
    return;

  overlay = my_window_get_grab_overlay (actor);
  if (!overlay || !CLUTTER_IS_ACTOR (overlay->container))
    return;

  width = (gfloat) frame.width;
  height = (gfloat) frame.height;
  csd = my_window_is_client_decorated (window);
  top_inset = csd ? TITLEBAR_HEIGHT : border;

  clutter_actor_set_position (overlay->container, (gfloat) frame.x, (gfloat) frame.y);
  clutter_actor_set_size (overlay->container, width, height);
  clutter_actor_show (overlay->container);

  my_window_place_grab_actor (overlay->north_west, 0.0f, 0.0f, border, border);
  my_window_place_grab_actor (overlay->north_east,
                              width - border,
                              0.0f,
                              border,
                              border);
  my_window_place_grab_actor (overlay->south_west,
                              0.0f,
                              height - border,
                              border,
                              border);
  my_window_place_grab_actor (overlay->south_east,
                              width - border,
                              height - border,
                              border,
                              border);
  my_window_place_grab_actor (overlay->north,
                              border,
                              0.0f,
                              width - 2.0f * border,
                              border);
  my_window_place_grab_actor (overlay->south,
                              border,
                              height - border,
                              width - 2.0f * border,
                              border);
  my_window_place_grab_actor (overlay->west,
                              0.0f,
                              top_inset,
                              border,
                              height - top_inset - border);
  my_window_place_grab_actor (overlay->east,
                              width - border,
                              top_inset,
                              border,
                              height - top_inset - border);

  if (csd)
    {
      my_window_place_grab_actor (overlay->titlebar,
                                  border,
                                  0.0f,
                                  width - 2.0f * border,
                                  TITLEBAR_HEIGHT);
    }
  else if (overlay->titlebar)
    {
      clutter_actor_hide (overlay->titlebar);
    }
}

void
my_window_setup (MetaWindowActor *actor)
{
  MetaWindow *window;
  ClutterActor *window_actor;
  ClutterActor *window_group;
  MyWindowGrabOverlay *overlay;

  if (g_object_get_data (G_OBJECT (actor), "my-window-setup"))
    return;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);
  window_group = clutter_actor_get_parent (window_actor);
  if (!window_group)
    return;

  /* Wayland/CSD clients handle their own move/resize; injecting grab
   * overlays into window_group fights the client and has crashed mutter. */
  if (my_window_is_client_decorated (window))
    return;

  overlay = g_new0 (MyWindowGrabOverlay, 1);
  g_weak_ref_init (&overlay->window_ref, window);

  overlay->container = clutter_actor_new ();
  clutter_actor_set_reactive (overlay->container, FALSE);
  clutter_actor_hide (overlay->container);

  overlay->north = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_N);
  overlay->south = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_S);
  overlay->east = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_E);
  overlay->west = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_W);
  overlay->north_west = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_NW);
  overlay->north_east = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_NE);
  overlay->south_west = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_SW);
  overlay->south_east = my_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_SE);
  overlay->titlebar = my_window_create_grab_actor (overlay, META_GRAB_OP_MOVING);

  clutter_actor_add_child (overlay->container, overlay->north);
  clutter_actor_add_child (overlay->container, overlay->south);
  clutter_actor_add_child (overlay->container, overlay->east);
  clutter_actor_add_child (overlay->container, overlay->west);
  clutter_actor_add_child (overlay->container, overlay->north_west);
  clutter_actor_add_child (overlay->container, overlay->north_east);
  clutter_actor_add_child (overlay->container, overlay->south_west);
  clutter_actor_add_child (overlay->container, overlay->south_east);
  clutter_actor_add_child (overlay->container, overlay->titlebar);

  clutter_actor_add_child (window_group, overlay->container);
  clutter_actor_set_child_above_sibling (window_group,
                                         overlay->container,
                                         window_actor);

  g_object_set_data_full (G_OBJECT (actor),
                          "my-window-grab-overlay",
                          overlay,
                          (GDestroyNotify) my_window_destroy_grab_overlay);

  overlay->position_changed_id =
    g_signal_connect_swapped (window,
                              "position-changed",
                              G_CALLBACK (my_window_schedule_sync),
                              actor);

  g_object_set_data (G_OBJECT (actor), "my-window-setup", GINT_TO_POINTER (1));
}

void
my_window_teardown (MetaWindowActor *actor)
{
  MyWindowGrabOverlay *overlay;

  my_window_cancel_scheduled_sync (actor);
  overlay = g_object_steal_data (G_OBJECT (actor), "my-window-grab-overlay");
  my_window_destroy_grab_overlay (overlay);
  g_object_set_data (G_OBJECT (actor), "my-window-setup", NULL);
}
