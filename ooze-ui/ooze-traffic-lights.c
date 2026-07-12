#include "ooze-traffic-lights.h"

#include "aqua-chrome.h"

#include <math.h>

struct _OozeTrafficLights
{
  GtkWidget parent_instance;
  GtkWindow *window;
};

G_DEFINE_FINAL_TYPE (OozeTrafficLights, ooze_traffic_lights, GTK_TYPE_WIDGET)

typedef enum
{
  OOZE_TRAFFIC_NONE = -1,
  OOZE_TRAFFIC_CLOSE,
  OOZE_TRAFFIC_MINIMIZE,
  OOZE_TRAFFIC_ZOOM,
} OozeTrafficButton;

static void
ooze_traffic_lights_measure (GtkWidget      *widget G_GNUC_UNUSED,
                             GtkOrientation  orientation,
                             int             for_size G_GNUC_UNUSED,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  int width;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      width = AQUA_TRAFFIC_LIGHT_MARGIN * 2 +
              AQUA_TRAFFIC_LIGHT_SIZE * 3 +
              AQUA_TRAFFIC_LIGHT_GAP * 2;
      *minimum = *natural = width;
    }
  else
    {
      *minimum = *natural = AQUA_TITLEBAR_HEIGHT;
    }

  if (minimum_baseline)
    *minimum_baseline = -1;
  if (natural_baseline)
    *natural_baseline = -1;
}

static OozeTrafficButton
ooze_traffic_lights_hit_test (OozeTrafficLights *self G_GNUC_UNUSED,
                              gdouble            x,
                              gdouble            y)
{
  gdouble cx;
  gdouble radius;

  if (y < 0 || y > gtk_widget_get_height (GTK_WIDGET (self)))
    return OOZE_TRAFFIC_NONE;

  cx = AQUA_TRAFFIC_LIGHT_MARGIN + AQUA_TRAFFIC_LIGHT_SIZE / 2.0;
  radius = AQUA_TRAFFIC_LIGHT_SIZE / 2.0 + 2.0;

  if (fabs (x - cx) <= radius)
    return OOZE_TRAFFIC_CLOSE;

  cx += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  if (fabs (x - cx) <= radius)
    return OOZE_TRAFFIC_MINIMIZE;

  cx += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  if (fabs (x - cx) <= radius)
    return OOZE_TRAFFIC_ZOOM;

  return OOZE_TRAFFIC_NONE;
}

static void
ooze_traffic_lights_activate (OozeTrafficLights  *self,
                              OozeTrafficButton   button)
{
  if (!self->window)
    return;

  switch (button)
    {
    case OOZE_TRAFFIC_CLOSE:
      gtk_window_close (self->window);
      break;
    case OOZE_TRAFFIC_MINIMIZE:
      gtk_window_minimize (self->window);
      break;
    case OOZE_TRAFFIC_ZOOM:
      if (gtk_window_is_maximized (self->window))
        gtk_window_unmaximize (self->window);
      else
        gtk_window_maximize (self->window);
      break;
    default:
      break;
    }
}

static void
ooze_traffic_lights_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  cairo_t *cr;
  gdouble cx;
  gdouble cy;

  cy = gtk_widget_get_height (widget) / 2.0;
  if (cy < 1.0)
    cy = AQUA_TITLEBAR_HEIGHT / 2.0;

  cr = gtk_snapshot_append_cairo (snapshot,
                                  &GRAPHENE_RECT_INIT (0, 0,
                                                       gtk_widget_get_width (widget),
                                                       gtk_widget_get_height (widget)));

  cx = AQUA_TRAFFIC_LIGHT_MARGIN + AQUA_TRAFFIC_LIGHT_SIZE / 2.0;
  aqua_traffic_draw_circle (cr, cx, cy, AQUA_TRAFFIC_LIGHT_SIZE,
                            AQUA_TRAFFIC_CLOSE_R,
                            AQUA_TRAFFIC_CLOSE_G,
                            AQUA_TRAFFIC_CLOSE_B);

  cx += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  aqua_traffic_draw_circle (cr, cx, cy, AQUA_TRAFFIC_LIGHT_SIZE,
                            AQUA_TRAFFIC_MINIMIZE_R,
                            AQUA_TRAFFIC_MINIMIZE_G,
                            AQUA_TRAFFIC_MINIMIZE_B);

  cx += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;
  aqua_traffic_draw_circle (cr, cx, cy, AQUA_TRAFFIC_LIGHT_SIZE,
                            AQUA_TRAFFIC_ZOOM_R,
                            AQUA_TRAFFIC_ZOOM_G,
                            AQUA_TRAFFIC_ZOOM_B);

  cairo_destroy (cr);
}

static gboolean
ooze_traffic_lights_click (GtkGestureClick *gesture,
                           int              n_press,
                           gdouble          x,
                           gdouble          y,
                           OozeTrafficLights *self)
{
  OozeTrafficButton button;

  if (n_press != 1 || gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture)) != GDK_BUTTON_PRIMARY)
    return FALSE;

  button = ooze_traffic_lights_hit_test (self, x, y);
  if (button == OOZE_TRAFFIC_NONE)
    return FALSE;

  ooze_traffic_lights_activate (self, button);
  return TRUE;
}

static void
ooze_traffic_lights_dispose (GObject *object)
{
  OozeTrafficLights *self = OOZE_TRAFFIC_LIGHTS (object);

  self->window = NULL;
  G_OBJECT_CLASS (ooze_traffic_lights_parent_class)->dispose (object);
}

static void
ooze_traffic_lights_class_init (OozeTrafficLightsClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ooze_traffic_lights_dispose;
  widget_class->measure = ooze_traffic_lights_measure;
  widget_class->snapshot = ooze_traffic_lights_snapshot;
}

static void
ooze_traffic_lights_init (OozeTrafficLights *self)
{
  GtkGesture *gesture;

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "default");
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, AQUA_TRAFFIC_LIGHT_SIZE + 4);

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect (gesture, "pressed", G_CALLBACK (ooze_traffic_lights_click), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (gesture));
}

OozeTrafficLights *
ooze_traffic_lights_new (void)
{
  return g_object_new (OOZE_TYPE_TRAFFIC_LIGHTS, NULL);
}

void
ooze_traffic_lights_attach_window (OozeTrafficLights *self,
                                   GtkWindow         *window)
{
  g_return_if_fail (OOZE_IS_TRAFFIC_LIGHTS (self));
  g_return_if_fail (GTK_IS_WINDOW (window));

  self->window = window;
}
