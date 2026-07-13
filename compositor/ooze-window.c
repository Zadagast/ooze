#include "ooze-window.h"

#include "aqua-chrome.h"

#include <graphene.h>
#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-enums.h>
#include <meta/window.h>

#define RESIZE_BORDER    8
#define TITLEBAR_HEIGHT  AQUA_TITLEBAR_HEIGHT
/* Mutter often suppresses size/position signals mid-resize; poll overlays. */
#define RESIZE_GRAB_SYNC_MS 16

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
} OozeWindowGrabOverlay;

typedef struct
{
  MetaDisplay *display;
  gulong grab_begin_id;
  gulong grab_end_id;
  guint poll_id;
  GWeakRef window_ref;
  GWeakRef match_ref;
  ClutterActor *seam_disabled; /* match strip made non-reactive during grab */
} OozeResizeGrabState;

static OozeResizeGrabState resize_grab;

gboolean
ooze_window_is_client_decorated (MetaWindow *window)
{
  if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_WAYLAND)
    return TRUE;

  if (meta_window_get_gtk_application_id (window) != NULL)
    return TRUE;

  return FALSE;
}

gboolean
ooze_window_uses_ooze_client_chrome (MetaWindow *window)
{
  const char *app_id;

  app_id = meta_window_get_gtk_application_id (window);
  return app_id && g_str_has_prefix (app_id, "org.ooze.");
}

static void
ooze_window_event_stage_point (ClutterEvent     *event,
                             graphene_point_t *point)
{
  clutter_event_get_position (event, point);
}

static gboolean
ooze_window_begin_grab (MetaWindow       *window,
                      ClutterEvent     *event,
                      MetaGrabOp         op)
{
  graphene_point_t pos_hint;

  if (op == META_GRAB_OP_NONE)
    return FALSE;

  ooze_window_event_stage_point (event, &pos_hint);
  meta_window_focus (window, clutter_event_get_time (event));

  return meta_window_begin_grab_op (window,
                                   op,
                                   NULL,
                                   clutter_event_get_time (event),
                                   &pos_hint);
}

gboolean
ooze_window_begin_grab_from_event (MetaWindow   *window,
                                 ClutterEvent *event,
                                 MetaGrabOp     op)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return FALSE;

  return ooze_window_begin_grab (window, event, op);
}

static gboolean
on_grab_overlay_press (ClutterActor *actor,
                       ClutterEvent *event,
                       MetaGrabOp    *op_ptr)
{
  OozeWindowGrabOverlay *overlay;
  MetaWindow *window;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  overlay = g_object_get_data (G_OBJECT (actor), "grab-overlay");
  if (!overlay)
    return CLUTTER_EVENT_PROPAGATE;

  window = g_weak_ref_get (&overlay->window_ref);
  if (!window)
    return CLUTTER_EVENT_PROPAGATE;

  if (ooze_window_begin_grab (window, event, *op_ptr))
    {
      g_object_unref (window);
      return CLUTTER_EVENT_STOP;
    }

  g_object_unref (window);
  return CLUTTER_EVENT_PROPAGATE;
}

static ClutterActor *
ooze_window_create_grab_actor (OozeWindowGrabOverlay *overlay,
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
ooze_window_destroy_grab_overlay (OozeWindowGrabOverlay *overlay)
{
  MetaWindow *window;

  if (!overlay)
    return;

  if (resize_grab.seam_disabled &&
      (resize_grab.seam_disabled == overlay->east ||
       resize_grab.seam_disabled == overlay->west ||
       resize_grab.seam_disabled == overlay->north ||
       resize_grab.seam_disabled == overlay->south ||
       resize_grab.seam_disabled == overlay->north_east ||
       resize_grab.seam_disabled == overlay->north_west ||
       resize_grab.seam_disabled == overlay->south_east ||
       resize_grab.seam_disabled == overlay->south_west))
    resize_grab.seam_disabled = NULL;

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
ooze_window_place_grab_actor (ClutterActor *actor,
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

static OozeWindowGrabOverlay *
ooze_window_get_grab_overlay (MetaWindowActor *actor)
{
  return g_object_get_data (G_OBJECT (actor), "ooze-window-grab-overlay");
}

static OozeWindowGrabOverlay *
ooze_window_overlay_for_meta_window (MetaWindow *window)
{
  MetaWindowActor *actor;

  if (!window)
    return NULL;

  actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!actor || meta_window_actor_is_destroyed (actor))
    return NULL;

  return ooze_window_get_grab_overlay (actor);
}

static gboolean
ooze_grab_op_is_resizing (MetaGrabOp op)
{
  op &= ~(MetaGrabOp) META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED;
  return (op & META_GRAB_OP_WINDOW_DIR_MASK) != 0;
}

static gboolean
ooze_grab_op_has_dir (MetaGrabOp op,
                      guint       dir)
{
  op &= ~(MetaGrabOp) META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED;
  return (op & dir) != 0;
}

static void
ooze_window_sync_meta_window (MetaWindow *window)
{
  MetaWindowActor *actor;

  if (!window)
    return;

  actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (!actor || meta_window_actor_is_destroyed (actor))
    return;

  ooze_window_sync (actor);
}

static void
ooze_window_restore_seam_strip (void)
{
  if (resize_grab.seam_disabled)
    {
      clutter_actor_set_reactive (resize_grab.seam_disabled, TRUE);
      resize_grab.seam_disabled = NULL;
    }
}

static void
ooze_window_disable_match_seam_strip (MetaWindow *match,
                                      MetaGrabOp  op)
{
  OozeWindowGrabOverlay *match_overlay;
  ClutterActor *strip = NULL;

  ooze_window_restore_seam_strip ();

  if (!match)
    return;

  match_overlay = ooze_window_overlay_for_meta_window (match);
  if (!match_overlay)
    return;

  /* Shared vertical seam: hide the match’s opposing E/W hit target. */
  if (ooze_grab_op_has_dir (op, META_GRAB_OP_WINDOW_DIR_EAST))
    strip = match_overlay->west;
  else if (ooze_grab_op_has_dir (op, META_GRAB_OP_WINDOW_DIR_WEST))
    strip = match_overlay->east;

  if (!strip || !CLUTTER_IS_ACTOR (strip))
    return;

  clutter_actor_set_reactive (strip, FALSE);
  resize_grab.seam_disabled = strip;
}

static void
ooze_window_stop_resize_grab_poll (void)
{
  if (resize_grab.poll_id)
    {
      g_source_remove (resize_grab.poll_id);
      resize_grab.poll_id = 0;
    }

  ooze_window_restore_seam_strip ();
  g_weak_ref_set (&resize_grab.window_ref, NULL);
  g_weak_ref_set (&resize_grab.match_ref, NULL);
}

static gboolean
ooze_window_resize_grab_poll (gpointer user_data G_GNUC_UNUSED)
{
  MetaWindow *window;
  MetaWindow *match;

  window = g_weak_ref_get (&resize_grab.window_ref);
  match = g_weak_ref_get (&resize_grab.match_ref);

  if (!window)
    {
      g_clear_object (&match);
      ooze_window_restore_seam_strip ();
      g_weak_ref_set (&resize_grab.window_ref, NULL);
      g_weak_ref_set (&resize_grab.match_ref, NULL);
      /* Do not g_source_remove while this timeout is running. */
      resize_grab.poll_id = 0;
      return G_SOURCE_REMOVE;
    }

  ooze_window_sync_meta_window (window);
  if (match)
    ooze_window_sync_meta_window (match);

  g_object_unref (window);
  g_clear_object (&match);
  return G_SOURCE_CONTINUE;
}

static void
ooze_window_on_grab_op_begin (MetaDisplay *display G_GNUC_UNUSED,
                              MetaWindow  *window,
                              MetaGrabOp   op,
                              gpointer     user_data G_GNUC_UNUSED)
{
  MetaWindow *match;

  if (!window || !ooze_grab_op_is_resizing (op))
    return;

  match = meta_window_get_tile_match (window);

  /* Only SSD overlays need poll-sync; CSD owns its own edges. */
  if (!ooze_window_overlay_for_meta_window (window) &&
      !ooze_window_overlay_for_meta_window (match))
    return;

  ooze_window_stop_resize_grab_poll ();

  g_weak_ref_set (&resize_grab.window_ref, window);
  g_weak_ref_set (&resize_grab.match_ref, match);

  if (match)
    ooze_window_disable_match_seam_strip (match, op);

  ooze_window_sync_meta_window (window);
  if (match)
    ooze_window_sync_meta_window (match);

  resize_grab.poll_id =
    g_timeout_add (RESIZE_GRAB_SYNC_MS, ooze_window_resize_grab_poll, NULL);
}

static void
ooze_window_on_grab_op_end (MetaDisplay *display G_GNUC_UNUSED,
                            MetaWindow  *window G_GNUC_UNUSED,
                            MetaGrabOp   op G_GNUC_UNUSED,
                            gpointer     user_data G_GNUC_UNUSED)
{
  MetaWindow *grabbed;
  MetaWindow *match;

  grabbed = g_weak_ref_get (&resize_grab.window_ref);
  match = g_weak_ref_get (&resize_grab.match_ref);

  ooze_window_stop_resize_grab_poll ();

  if (grabbed)
    {
      ooze_window_sync_meta_window (grabbed);
      g_object_unref (grabbed);
    }
  if (match)
    {
      ooze_window_sync_meta_window (match);
      g_object_unref (match);
    }
}

void
ooze_window_connect_display (MetaDisplay *display)
{
  if (!display || resize_grab.display == display)
    return;

  if (resize_grab.display)
    ooze_window_disconnect_display (resize_grab.display);

  resize_grab.display = display;
  g_weak_ref_init (&resize_grab.window_ref, NULL);
  g_weak_ref_init (&resize_grab.match_ref, NULL);

  resize_grab.grab_begin_id =
    g_signal_connect (display,
                      "grab-op-begin",
                      G_CALLBACK (ooze_window_on_grab_op_begin),
                      NULL);
  resize_grab.grab_end_id =
    g_signal_connect (display,
                      "grab-op-end",
                      G_CALLBACK (ooze_window_on_grab_op_end),
                      NULL);
}

void
ooze_window_disconnect_display (MetaDisplay *display)
{
  if (!display || resize_grab.display != display)
    return;

  ooze_window_stop_resize_grab_poll ();

  if (resize_grab.grab_begin_id)
    {
      g_signal_handler_disconnect (display, resize_grab.grab_begin_id);
      resize_grab.grab_begin_id = 0;
    }
  if (resize_grab.grab_end_id)
    {
      g_signal_handler_disconnect (display, resize_grab.grab_end_id);
      resize_grab.grab_end_id = 0;
    }

  g_weak_ref_clear (&resize_grab.window_ref);
  g_weak_ref_clear (&resize_grab.match_ref);
  resize_grab.display = NULL;
}

void
ooze_window_cancel_scheduled_sync (MetaWindowActor *actor)
{
  gpointer sync_id = g_object_get_data (G_OBJECT (actor), "ooze-window-sync-id");

  if (sync_id)
    {
      g_source_remove (GPOINTER_TO_UINT (sync_id));
      g_object_set_data (G_OBJECT (actor), "ooze-window-sync-id", NULL);
    }
}

static gboolean
ooze_window_sync_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  g_object_set_data (G_OBJECT (actor), "ooze-window-sync-id", NULL);
  ooze_window_sync (actor);
  return G_SOURCE_REMOVE;
}

void
ooze_window_schedule_sync (MetaWindowActor *actor)
{
  if (g_object_get_data (G_OBJECT (actor), "ooze-window-sync-id"))
    return;

  g_object_ref (actor);
  g_object_set_data (G_OBJECT (actor),
                     "ooze-window-sync-id",
                     GUINT_TO_POINTER (g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                        ooze_window_sync_idle,
                                                        actor,
                                                        g_object_unref)));
}

void
ooze_window_sync (MetaWindowActor *actor)
{
  MetaWindow *window;
  OozeWindowGrabOverlay *overlay;
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

  overlay = ooze_window_get_grab_overlay (actor);
  if (!overlay || !CLUTTER_IS_ACTOR (overlay->container))
    return;

  width = (gfloat) frame.width;
  height = (gfloat) frame.height;
  csd = ooze_window_is_client_decorated (window);
  top_inset = csd ? TITLEBAR_HEIGHT : border;

  clutter_actor_set_position (overlay->container, (gfloat) frame.x, (gfloat) frame.y);
  clutter_actor_set_size (overlay->container, width, height);
  clutter_actor_show (overlay->container);

  ooze_window_place_grab_actor (overlay->north_west, 0.0f, 0.0f, border, border);
  ooze_window_place_grab_actor (overlay->north_east,
                              width - border,
                              0.0f,
                              border,
                              border);
  ooze_window_place_grab_actor (overlay->south_west,
                              0.0f,
                              height - border,
                              border,
                              border);
  ooze_window_place_grab_actor (overlay->south_east,
                              width - border,
                              height - border,
                              border,
                              border);
  ooze_window_place_grab_actor (overlay->north,
                              border,
                              0.0f,
                              width - 2.0f * border,
                              border);
  ooze_window_place_grab_actor (overlay->south,
                              border,
                              height - border,
                              width - 2.0f * border,
                              border);
  ooze_window_place_grab_actor (overlay->west,
                              0.0f,
                              top_inset,
                              border,
                              height - top_inset - border);
  ooze_window_place_grab_actor (overlay->east,
                              width - border,
                              top_inset,
                              border,
                              height - top_inset - border);

  if (csd)
    {
      ooze_window_place_grab_actor (overlay->titlebar,
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
ooze_window_setup (MetaWindowActor *actor)
{
  MetaWindow *window;
  ClutterActor *window_actor;
  ClutterActor *window_group;
  OozeWindowGrabOverlay *overlay;

  if (g_object_get_data (G_OBJECT (actor), "ooze-window-setup"))
    return;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);
  window_group = clutter_actor_get_parent (window_actor);
  if (!window_group)
    return;

  /*
   * Wayland / GTK CSD (Inkscape, Ooze Gel, …) own move/resize. Injecting
   * grab overlays into window_group fights the client and crashes Mutter.
   * Foreign CSD edge-tile: Super+Left/Right + larger nest work-area.
   */
  if (ooze_window_is_client_decorated (window))
    return;

  overlay = g_new0 (OozeWindowGrabOverlay, 1);
  g_weak_ref_init (&overlay->window_ref, window);

  overlay->container = clutter_actor_new ();
  clutter_actor_set_reactive (overlay->container, FALSE);
  clutter_actor_hide (overlay->container);

  overlay->north = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_N);
  overlay->south = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_S);
  overlay->east = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_E);
  overlay->west = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_W);
  overlay->north_west = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_NW);
  overlay->north_east = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_NE);
  overlay->south_west = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_SW);
  overlay->south_east = ooze_window_create_grab_actor (overlay, META_GRAB_OP_RESIZING_SE);
  overlay->titlebar = ooze_window_create_grab_actor (overlay, META_GRAB_OP_MOVING);

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
                          "ooze-window-grab-overlay",
                          overlay,
                          (GDestroyNotify) ooze_window_destroy_grab_overlay);

  overlay->position_changed_id =
    g_signal_connect_swapped (window,
                              "position-changed",
                              G_CALLBACK (ooze_window_schedule_sync),
                              actor);

  g_object_set_data (G_OBJECT (actor), "ooze-window-setup", GINT_TO_POINTER (1));
}

void
ooze_window_teardown (MetaWindowActor *actor)
{
  OozeWindowGrabOverlay *overlay;

  ooze_window_cancel_scheduled_sync (actor);
  overlay = g_object_steal_data (G_OBJECT (actor), "ooze-window-grab-overlay");
  ooze_window_destroy_grab_overlay (overlay);
  g_object_set_data (G_OBJECT (actor), "ooze-window-setup", NULL);
}
