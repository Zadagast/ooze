#include "ooze-flow.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
  gdouble x;
  gdouble y;
  gdouble radius;
  gdouble speed;
  gdouble phase;
  gdouble x_scale;
  gdouble y_scale;
} OozeFlowBlob;

/* x, y_center, radius, speed, phase, x_sway, y_amplitude.
 * Lava lamp: fat blobs travelling slowly up/down with a gentle sway. */
static const OozeFlowBlob flow_blobs[] = {
  { 0.10, 0.50, 0.20, 0.62, 0.10, 0.05, 0.40 },
  { 0.22, 0.46, 0.17, 0.50, 1.80, 0.04, 0.44 },
  { 0.34, 0.53, 0.23, 0.72, 3.25, 0.06, 0.36 },
  { 0.46, 0.48, 0.18, 0.55, 4.40, 0.04, 0.42 },
  { 0.58, 0.52, 0.21, 0.64, 5.10, 0.05, 0.38 },
  { 0.70, 0.47, 0.17, 0.50, 2.55, 0.04, 0.45 },
  { 0.82, 0.53, 0.22, 0.58, 0.90, 0.05, 0.34 },
  { 0.92, 0.49, 0.16, 0.44, 5.70, 0.04, 0.43 },
  { 0.16, 0.54, 0.22, 0.68, 2.10, 0.06, 0.37 },
  { 0.28, 0.49, 0.17, 0.52, 3.90, 0.04, 0.46 },
  { 0.40, 0.45, 0.20, 0.60, 1.30, 0.05, 0.39 },
  { 0.52, 0.54, 0.18, 0.66, 0.35, 0.04, 0.41 },
  { 0.64, 0.50, 0.21, 0.40, 2.90, 0.05, 0.35 },
  { 0.76, 0.46, 0.17, 0.56, 4.10, 0.05, 0.44 },
  { 0.88, 0.52, 0.19, 0.63, 0.60, 0.05, 0.40 },
  { 0.06, 0.48, 0.18, 0.47, 3.50, 0.04, 0.36 },
  { 0.50, 0.47, 0.23, 0.71, 1.10, 0.06, 0.38 },
  { 0.72, 0.54, 0.19, 0.53, 5.30, 0.05, 0.43 },
};

static gdouble
flow_smoothstep (gdouble edge0,
                 gdouble edge1,
                 gdouble value)
{
  gdouble t;

  if (edge0 == edge1)
    return value < edge0 ? 0.0 : 1.0;

  t = CLAMP ((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

static void
flow_blob_position (const OozeFlowBlob *blob,
                    gdouble             phase,
                    gdouble            *x_out,
                    gdouble            *y_out)
{
  gdouble t = phase * blob->speed + blob->phase;

  /* Lava lamp: slow vertical rise/sink, only a gentle horizontal sway. */
  *y_out = blob->y + blob->y_scale * sin (t);
  *x_out = blob->x +
           blob->x_scale * (sin (t * 0.53 + blob->phase) +
                            0.5 * sin (t * 0.27 + blob->phase * 1.7));
}

void
ooze_flow_render_with_color (cairo_surface_t *surface,
                             int              width,
                             int              height,
                             gdouble          phase,
                             gboolean         dark,
                             gdouble          base_red,
                             gdouble          base_green,
                             gdouble          base_blue)
{
  cairo_format_t format;
  int surface_width;
  int surface_height;
  int stride;
  unsigned char *data;
  gdouble aspect;
  gdouble blob_positions[G_N_ELEMENTS (flow_blobs)][2];
  int y;

  g_return_if_fail (surface != NULL);

  format = cairo_image_surface_get_format (surface);
  g_return_if_fail (format == CAIRO_FORMAT_ARGB32);

  surface_width = cairo_image_surface_get_width (surface);
  surface_height = cairo_image_surface_get_height (surface);
  width = CLAMP (width, 1, surface_width);
  height = CLAMP (height, 1, surface_height);

  cairo_surface_flush (surface);
  stride = cairo_image_surface_get_stride (surface);
  data = cairo_image_surface_get_data (surface);
  memset (data, 0, (gsize) stride * surface_height);

  aspect = (gdouble) width / (gdouble) height;

  for (gsize i = 0; i < G_N_ELEMENTS (flow_blobs); i++)
    flow_blob_position (&flow_blobs[i],
                        phase,
                        &blob_positions[i][0],
                        &blob_positions[i][1]);

  for (y = 0; y < height; y++)
    {
      guint32 *row = (guint32 *) (data + y * stride);
      gdouble normalized_y = ((gdouble) y + 0.5) / height;
      int x;

      for (x = 0; x < width; x++)
        {
          gdouble normalized_x = ((gdouble) x + 0.5) / width;
          gdouble field = 0.0;
          gdouble specular = 0.0;
          gdouble rim;
          gdouble glass;
          gdouble red;
          gdouble green;
          gdouble blue;
          gdouble alpha;
          guint8 a;
          guint8 r;
          guint8 g;
          guint8 b;
          gsize i;

          for (i = 0; i < G_N_ELEMENTS (flow_blobs); i++)
            {
              gdouble blob_x = blob_positions[i][0];
              gdouble blob_y = blob_positions[i][1];
              gdouble dx;
              gdouble dy;
              gdouble distance;
              gdouble radius = flow_blobs[i].radius;
              gdouble highlight_x;
              gdouble highlight_y;
              gdouble highlight_distance;

              gdouble stretch = 1.0 + 0.24 * sin (phase * flow_blobs[i].speed *
                                                  0.7 + flow_blobs[i].phase);

              dx = (normalized_x - blob_x) * aspect;
              dy = (normalized_y - blob_y) / stretch;
              distance = (dx * dx + dy * dy) / (radius * radius);
              field += 0.21 / (distance + 0.05);

              highlight_x = blob_x - radius * 0.34;
              highlight_y = blob_y - radius * 0.38;
              highlight_distance =
                ((normalized_x - highlight_x) * aspect) *
                ((normalized_x - highlight_x) * aspect) +
                (normalized_y - highlight_y) *
                (normalized_y - highlight_y);
              specular += exp (-highlight_distance /
                               (radius * radius * 0.16));
            }

          alpha = flow_smoothstep (0.44, 1.45, field);
          if (alpha <= 0.001)
            continue;

          rim = flow_smoothstep (0.36, 0.82, field) -
                flow_smoothstep (1.08, 1.80, field);
          glass = CLAMP (flow_smoothstep (0.55, 1.35, field) * 0.35 +
                         rim * 0.55, 0.0, 0.78);
          alpha *= dark ? 0.50 : 0.56;
          specular = CLAMP (specular * 0.34, 0.0, 0.58);

          red = base_red + glass * 0.16 + specular;
          green = base_green + glass * 0.18 + specular * 1.10;
          blue = base_blue + glass * 0.24 + specular * 1.30;

          a = (guint8) CLAMP (alpha * 255.0, 0.0, 255.0);
          r = (guint8) CLAMP (red * alpha * 255.0, 0.0, 255.0);
          g = (guint8) CLAMP (green * alpha * 255.0, 0.0, 255.0);
          b = (guint8) CLAMP (blue * alpha * 255.0, 0.0, 255.0);
          row[x] = ((guint32) a << 24) |
                   ((guint32) r << 16) |
                   ((guint32) g << 8) |
                   b;
        }
    }

  cairo_surface_mark_dirty (surface);
}

void
ooze_flow_render (cairo_surface_t *surface,
                  int              width,
                  int              height,
                  gdouble          phase,
                  gboolean         dark)
{
  ooze_flow_render_with_color (surface,
                               width,
                               height,
                               phase,
                               dark,
                               dark ? 0.12 : 0.09,
                               dark ? 0.34 : 0.30,
                               dark ? 0.72 : 0.68);
}
