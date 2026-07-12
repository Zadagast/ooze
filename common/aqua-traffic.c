#include "aqua-chrome.h"

void
aqua_traffic_draw_circle (cairo_t *cr,
                          gdouble  cx,
                          gdouble  cy,
                          gdouble  size,
                          gdouble  r,
                          gdouble  g,
                          gdouble  b)
{
  gdouble radius;

  if (size < 1.0)
    size = 1.0;

  radius = size / 2.0 - 0.75;

  cairo_arc (cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_set_source_rgb (cr, r, g, b);
  cairo_fill_preserve (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.35);
  cairo_set_line_width (cr, 0.75);
  cairo_stroke (cr);

  cairo_arc (cr,
             cx - radius * 0.28,
             cy - radius * 0.32,
             radius * 0.22,
             0,
             2 * G_PI);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.45);
  cairo_fill (cr);
}
