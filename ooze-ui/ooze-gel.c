#include "ooze-gel.h"

#include <adwaita.h>
#include <cairo/cairo.h>
#include <gdk/gdk.h>

#define OOZE_RESIZE_BORDER 3
#define OOZE_SHADOW_PAD    10

typedef struct
{
  GtkWindow *window;
  GdkSurfaceEdge edge;
} OozeResizeEdge;

typedef struct
{
  GtkWindow *window;
} OozeDragHandle;

typedef struct _OozeShadowBin
{
  GtkBox parent_instance;
} OozeShadowBin;

typedef struct _OozeShadowBinClass
{
  GtkBoxClass parent_class;
} OozeShadowBinClass;

G_DEFINE_FINAL_TYPE (OozeShadowBin, ooze_shadow_bin, GTK_TYPE_BOX)

static void
ooze_resize_edge_free (gpointer data)
{
  g_free (data);
}

static void
ooze_drag_handle_free (gpointer data)
{
  g_free (data);
}

/* Draw a single rounded-rect path starting from top-left and going clockwise. */
static void
ooze_rounded_rect_path (cairo_t *cr,
                        gdouble  x,
                        gdouble  y,
                        gdouble  w,
                        gdouble  h,
                        gdouble  r)
{
  cairo_new_sub_path (cr);
  cairo_arc (cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
  cairo_arc (cr, x + w - r, y + r, r, 3 * G_PI / 2, 0);
  cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI / 2);
  cairo_arc (cr, x + r, y + h - r, r, G_PI / 2, G_PI);
  cairo_close_path (cr);
}

static void
ooze_shadow_bin_snapshot (GtkWidget   *widget,
                          GtkSnapshot *snapshot)
{
  gdouble width, height;
  gdouble radius;
  gdouble fx, fy, fw, fh;
  gboolean dark;
  cairo_t *cr;

  width  = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);
  if (width <= 0 || height <= 0)
    return;

  dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  radius = 7.0;

  /*
   * The visible frame sits slightly inside the shadow-bin's allocation.
   * The remaining space is used by the drop-shadow layers below.
   * shadow-bin has overflow:hidden so children are clipped to its bounds.
   */
  fx = 0.5;
  fy = 0.5;
  fw = width  - 4.0;
  fh = height - 6.0;

  cr = gtk_snapshot_append_cairo (snapshot,
                                  &GRAPHENE_RECT_INIT (0, 0, width, height));

  /* ── Drop shadow (three offset layers, bottom-right bias) ── */
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.30);
  ooze_rounded_rect_path (cr, fx + 2, fy + 5, fw, fh, radius);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.16);
  ooze_rounded_rect_path (cr, fx + 4, fy + 8, fw + 1, fh + 1, radius);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.07);
  ooze_rounded_rect_path (cr, fx + 6, fy + 11, fw + 2, fh + 2, radius);
  cairo_fill (cr);

  /*
   * ── Window frame ───────────────────────────────────────────
   *
   * Fill with the opaque window background so content has a solid
   * base even when ooze-shell-frame's CSS background is transparent.
   * The title-bar, toolbar etc. paint on top of this via their own
   * CSS backgrounds.  Colors follow the session light/dark; the
   * dark values match Adwaita's @window_bg_color so the Cairo frame
   * and the child libadwaita/OozeKit surfaces stay consistent.
   *
   * The frame border is a two-layer stroke:
   *   outer – ring for definition against the wallpaper
   *   inner – subtle highlight for the classic Aqua gloss rim
   */
  ooze_rounded_rect_path (cr, fx, fy, fw, fh, radius);
  if (dark)
    cairo_set_source_rgb (cr, 0.141, 0.141, 0.141);
  else
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_fill_preserve (cr);

  /* Outer border – frame edge */
  if (dark)
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.55);
  else
    cairo_set_source_rgba (cr, 0.33, 0.33, 0.38, 0.92);
  cairo_set_line_width (cr, 1.8);
  cairo_stroke_preserve (cr);

  /* Inner gloss rim – 1 px highlight just inside the border */
  ooze_rounded_rect_path (cr, fx + 1.5, fy + 1.5, fw - 3.0, fh - 3.0, radius - 1.0);
  if (dark)
    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.08);
  else
    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.60);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  cairo_destroy (cr);

  /*
   * Push a rounded clip so every child widget (ooze-shell-frame white
   * background, header bar, toolbar, etc.) is visually clipped to the
   * same rounded-rect shape drawn by Cairo above.  Without this the GTK
   * layout clips children to a plain rectangle, leaving square corners
   * visible over the rounded Cairo frame.
   */
  {
    GskRoundedRect clip;
    graphene_rect_t clip_rect =
      GRAPHENE_RECT_INIT ((float)(fx + 1.0),
                          (float)(fy + 1.0),
                          (float)(fw - 2.0),
                          (float)(fh - 2.0));
    gsk_rounded_rect_init_from_rect (&clip, &clip_rect, (float)(radius - 1.0));
    gtk_snapshot_push_rounded_clip (snapshot, &clip);
  }

  GTK_WIDGET_CLASS (ooze_shadow_bin_parent_class)->snapshot (widget, snapshot);

  gtk_snapshot_pop (snapshot);  /* rounded clip */
}

static void
ooze_shadow_bin_class_init (OozeShadowBinClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->snapshot = ooze_shadow_bin_snapshot;
}

static void
ooze_shadow_bin_on_dark_changed (AdwStyleManager *manager G_GNUC_UNUSED,
                                 GParamSpec      *pspec G_GNUC_UNUSED,
                                 OozeShadowBin   *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
ooze_shadow_bin_init (OozeShadowBin *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);

  /* Repaint the frame when the session light/dark changes. Tied to the
   * widget lifetime so it disconnects automatically on finalize. */
  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::dark",
                           G_CALLBACK (ooze_shadow_bin_on_dark_changed),
                           self,
                           0);
}

static GtkWidget *
ooze_shadow_bin_new (void)
{
  return g_object_new (ooze_shadow_bin_get_type (), NULL);
}

static gboolean
ooze_gel_begin_move (GtkWindow *window,
                        GdkDevice *device,
                        guint      button,
                        gdouble    x,
                        gdouble    y,
                        guint32    time)
{
  GdkSurface *surface;

  if (!window || !device)
    return FALSE;

  surface = gtk_native_get_surface (GTK_NATIVE (window));
  if (!surface)
    return FALSE;

  gdk_toplevel_begin_move (GDK_TOPLEVEL (surface),
                           device,
                           button,
                           x,
                           y,
                           time);
  return TRUE;
}

static gboolean
ooze_drag_handle_begin (GtkGestureDrag *gesture,
                        gdouble         start_x G_GNUC_UNUSED,
                        gdouble         start_y G_GNUC_UNUSED,
                        OozeDragHandle  *handle)
{
  GdkDevice *device;
  GdkEvent *event;
  gdouble x;
  gdouble y;
  guint button;
  guint32 time;

  if (!handle || !handle->window)
    return FALSE;

  device = gtk_gesture_get_device (GTK_GESTURE (gesture));
  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  if (button != GDK_BUTTON_PRIMARY)
    return FALSE;

  event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (gesture));
  if (!event)
    return FALSE;

  gdk_event_get_position (event, &x, &y);
  time = gdk_event_get_time (event);
  return ooze_gel_begin_move (handle->window, device, button, x, y, time);
}

static gboolean
ooze_resize_edge_drag_begin (GtkGestureDrag *gesture,
                             gdouble         start_x G_GNUC_UNUSED,
                             gdouble         start_y G_GNUC_UNUSED,
                             OozeResizeEdge  *edge)
{
  GdkDevice *device;
  GdkEvent *event;
  guint button;
  guint32 time;
  GdkSurface *surface;
  gdouble x;
  gdouble y;

  if (!edge || !edge->window)
    return FALSE;

  device = gtk_gesture_get_device (GTK_GESTURE (gesture));
  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  if (button != GDK_BUTTON_PRIMARY)
    return FALSE;

  event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (gesture));
  if (!event)
    return FALSE;

  surface = gtk_native_get_surface (GTK_NATIVE (edge->window));
  if (!surface)
    return FALSE;

  gdk_event_get_position (event, &x, &y);
  time = gdk_event_get_time (event);
  gdk_toplevel_begin_resize (GDK_TOPLEVEL (surface),
                             edge->edge,
                             device,
                             button,
                             x,
                             y,
                             time);
  return TRUE;
}

static GtkWidget *
ooze_resize_edge_new (GtkWindow      *window,
                      GdkSurfaceEdge  edge,
                      const char     *css_name,
                      const char     *cursor_name)
{
  GtkWidget *widget;
  OozeResizeEdge *data;
  GtkGesture *gesture;

  widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (widget, css_name);
  gtk_widget_set_can_target (widget, TRUE);
  if (cursor_name)
    gtk_widget_set_cursor_from_name (widget, cursor_name);

  switch (edge)
    {
    case GDK_SURFACE_EDGE_NORTH:
    case GDK_SURFACE_EDGE_SOUTH:
      gtk_widget_set_hexpand (widget, TRUE);
      break;
    case GDK_SURFACE_EDGE_WEST:
    case GDK_SURFACE_EDGE_EAST:
      gtk_widget_set_vexpand (widget, TRUE);
      break;
    default:
      break;
    }

  data = g_new (OozeResizeEdge, 1);
  data->window = window;
  data->edge = edge;
  g_object_set_data_full (G_OBJECT (widget), "ooze-resize-edge", data, ooze_resize_edge_free);

  gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect (gesture, "drag-begin", G_CALLBACK (ooze_resize_edge_drag_begin), data);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));

  return widget;
}

static void
ooze_gel_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GtkCssProvider *provider;
  GdkDisplay *display;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider,
                                     /* GTK window is fully transparent; Cairo draws the rounded Ooze Gel frame. */
                                     "window.ooze-framed-window {"
                                     "  background-color: transparent;"
                                     "  background-image: none;"
                                     "}"
                                     ".ooze-framed-window { background: transparent; }"

                                     /*
                                      * Grid padding gives room for the drop-shadow that the shadow-bin
                                      * draws beyond the visible frame.
                                      */
                                     ".ooze-shell-grid {"
                                     "  padding: 8px;"
                                     "  background: transparent;"
                                     "}"
                                     ".ooze-shell-center {"
                                     "  background: transparent;"
                                     "  padding: 0;"
                                     "}"

                                     /*
                                      * The shell-frame is an inner VBox child of the shadow-bin.
                                      * Cairo already fills the rounded rect with white, but we also
                                      * set it here so GTK's own layout background is consistent.
                                      * border: none – the Cairo stroke is the window border.
                                      */
                                     ".ooze-shell-frame {"
                                     "  background: @window_bg_color;"
                                     "  border: none;"
                                     "}"

                                     /*
                                      * Resize handle strips around the window.
                                      * They use the same dark-gray as the Cairo border stroke so that
                                      * visually they look like a continuous window border.
                                      * min-size 4 px is enough to be a comfortable drag target.
                                      */
                                     ".ooze-resize-n,"
                                     ".ooze-resize-s {"
                                     "  min-height: 4px;"
                                     "  background: rgba(85, 85, 97, 0.88);"
                                     "}"
                                     ".ooze-resize-e,"
                                     ".ooze-resize-w {"
                                     "  min-width: 4px;"
                                     "  background: rgba(85, 85, 97, 0.88);"
                                     "}"
                                     ".ooze-resize-ne,"
                                     ".ooze-resize-nw,"
                                     ".ooze-resize-se,"
                                     ".ooze-resize-sw {"
                                     "  min-width:  4px;"
                                     "  min-height: 4px;"
                                     "  background: rgba(85, 85, 97, 0.88);"
                                     "}"

                                     /* Invisible grips for normal Ooze CSD windows. */
                                     ".ooze-edge-grip {"
                                     "  background: transparent;"
                                     "}"
                                     ".ooze-edge-grip.n,"
                                     ".ooze-edge-grip.s {"
                                     "  min-height: 6px;"
                                     "}"
                                     ".ooze-edge-grip.e,"
                                     ".ooze-edge-grip.w {"
                                     "  min-width: 6px;"
                                     "}"
                                     ".ooze-edge-grip.corner {"
                                     "  min-width: 12px;"
                                     "  min-height: 12px;"
                                     "}"

                                     /*
                                      * WhiteSur / libadwaita gtk-4.0 CSS can inject traffic-light
                                      * windowcontrols globally. Ooze Gel draws its own buttons —
                                      * never show a second set on framed Ooze windows.
                                      */
                                     "window.ooze-framed-window windowcontrols,"
                                     "window.ooze-framed-window .titlebutton,"
                                     ".ooze-framed-window windowcontrols,"
                                     ".ooze-framed-window .titlebutton {"
                                     "  opacity: 0;"
                                     "  min-width: 0;"
                                     "  min-height: 0;"
                                     "  padding: 0;"
                                     "  margin: 0;"
                                     "  border: none;"
                                     "  background: none;"
                                     "  box-shadow: none;"
                                     "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER);
  g_object_unref (provider);
  loaded = TRUE;
}

static GtkWidget *
ooze_edge_grip_new (GtkWindow      *window,
                    GdkSurfaceEdge  edge,
                    const char     *extra_class,
                    const char     *cursor_name,
                    GtkAlign        halign,
                    GtkAlign        valign,
                    gboolean        hexpand,
                    gboolean        vexpand)
{
  GtkWidget *widget = ooze_resize_edge_new (window, edge, "ooze-edge-grip", cursor_name);

  if (extra_class)
    gtk_widget_add_css_class (widget, extra_class);
  gtk_widget_set_halign (widget, halign);
  gtk_widget_set_valign (widget, valign);
  gtk_widget_set_hexpand (widget, hexpand);
  gtk_widget_set_vexpand (widget, vexpand);
  return widget;
}

void
ooze_gel_install_edge_resize (GtkWindow *window)
{
  GtkWidget *child;
  GtkWidget *overlay;
  GtkWidget *grip;

  g_return_if_fail (GTK_IS_WINDOW (window));

  if (g_object_get_data (G_OBJECT (window), "ooze-edge-resize-installed"))
    return;

  child = gtk_window_get_child (window);
  if (!child)
    return;

  /* Don't double-wrap the heavy framed Ooze Gel path. */
  if (g_object_get_data (G_OBJECT (window), "ooze-resize-installed"))
    return;

  /* Mark early so notify::child from reparenting does not re-enter. */
  g_object_set_data (G_OBJECT (window), "ooze-edge-resize-installed", GINT_TO_POINTER (1));

  ooze_gel_ensure_css ();

  g_object_ref (child);
  gtk_window_set_child (window, NULL);

  overlay = gtk_overlay_new ();
  gtk_overlay_set_child (GTK_OVERLAY (overlay), child);
  g_object_unref (child);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_NORTH, "n", "n-resize",
                             GTK_ALIGN_FILL, GTK_ALIGN_START, TRUE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_SOUTH, "s", "s-resize",
                             GTK_ALIGN_FILL, GTK_ALIGN_END, TRUE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_WEST, "w", "w-resize",
                             GTK_ALIGN_START, GTK_ALIGN_FILL, FALSE, TRUE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_EAST, "e", "e-resize",
                             GTK_ALIGN_END, GTK_ALIGN_FILL, FALSE, TRUE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_NORTH_WEST, "corner", "nwse-resize",
                             GTK_ALIGN_START, GTK_ALIGN_START, FALSE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_NORTH_EAST, "corner", "nesw-resize",
                             GTK_ALIGN_END, GTK_ALIGN_START, FALSE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_SOUTH_WEST, "corner", "nesw-resize",
                             GTK_ALIGN_START, GTK_ALIGN_END, FALSE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  grip = ooze_edge_grip_new (window, GDK_SURFACE_EDGE_SOUTH_EAST, "corner", "nwse-resize",
                             GTK_ALIGN_END, GTK_ALIGN_END, FALSE, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), grip);

  gtk_window_set_child (window, overlay);
}

void
ooze_gel_install_resize_handles (GtkWindow *window,
                                    GtkWidget *root)
{
  GtkWidget *overlay;
  GtkWidget *grid;
  GtkWidget *center;
  GtkWidget *shadow_bin;
  GtkWidget *frame;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_WIDGET (root));

  ooze_gel_ensure_css ();

  if (g_object_get_data (G_OBJECT (window), "ooze-resize-installed"))
    return;

  gtk_widget_add_css_class (GTK_WIDGET (window), "ooze-framed-window");

  overlay = gtk_overlay_new ();
  grid = gtk_grid_new ();
  gtk_widget_add_css_class (grid, "ooze-shell-grid");
  center = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  shadow_bin = ooze_shadow_bin_new ();
  frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  gtk_widget_add_css_class (center, "ooze-shell-center");
  gtk_widget_add_css_class (frame, "ooze-shell-frame");
  gtk_widget_set_hexpand (shadow_bin, TRUE);
  gtk_widget_set_vexpand (shadow_bin, TRUE);
  gtk_widget_set_hexpand (frame, TRUE);
  gtk_widget_set_vexpand (frame, TRUE);
  gtk_box_append (GTK_BOX (frame), root);
  gtk_widget_set_hexpand (root, TRUE);
  gtk_widget_set_vexpand (root, TRUE);
  gtk_box_append (GTK_BOX (shadow_bin), frame);
  gtk_box_append (GTK_BOX (center), shadow_bin);
  gtk_widget_set_hexpand (center, TRUE);
  gtk_widget_set_vexpand (center, TRUE);

  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_NORTH_WEST, "ooze-resize-nw", "nwse-resize"), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_NORTH, "ooze-resize-n", "n-resize"), 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_NORTH_EAST, "ooze-resize-ne", "nesw-resize"), 2, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_WEST, "ooze-resize-w", "w-resize"), 0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), center, 1, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_EAST, "ooze-resize-e", "e-resize"), 2, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_SOUTH_WEST, "ooze-resize-sw", "nesw-resize"), 0, 2, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_SOUTH, "ooze-resize-s", "s-resize"), 1, 2, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), ooze_resize_edge_new (window, GDK_SURFACE_EDGE_SOUTH_EAST, "ooze-resize-se", "nwse-resize"), 2, 2, 1, 1);

  gtk_widget_set_hexpand (center, TRUE);
  gtk_widget_set_vexpand (center, TRUE);

  gtk_overlay_set_child (GTK_OVERLAY (overlay), grid);
  if (ADW_IS_APPLICATION_WINDOW (window))
    adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), overlay);
  else
    gtk_window_set_child (window, overlay);

  g_object_set_data (G_OBJECT (window), "ooze-resize-installed", GINT_TO_POINTER (1));
}

void
ooze_gel_install_drag (GtkWidget *widget,
                          GtkWindow *window)
{
  OozeDragHandle *handle;
  GtkGesture *gesture;

  g_return_if_fail (GTK_IS_WIDGET (widget));
  g_return_if_fail (GTK_IS_WINDOW (window));

  if (g_object_get_data (G_OBJECT (widget), "ooze-drag-installed"))
    return;

  handle = g_new (OozeDragHandle, 1);
  handle->window = window;
  g_object_set_data_full (G_OBJECT (widget), "ooze-drag-handle", handle, ooze_drag_handle_free);

  gtk_widget_set_can_target (widget, TRUE);

  gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect (gesture, "drag-begin", G_CALLBACK (ooze_drag_handle_begin), handle);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));

  g_object_set_data (G_OBJECT (widget), "ooze-drag-installed", GINT_TO_POINTER (1));
}
