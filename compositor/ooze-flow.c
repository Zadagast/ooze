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

static const OozeFlowBlob flow_blobs[] = {
  { 0.23, 0.32, 0.17, 0.72, 0.10, 0.17, 0.13 },
  { 0.42, 0.27, 0.14, 0.51, 1.80, 0.14, 0.18 },
  { 0.62, 0.39, 0.18, 0.63, 3.25, 0.18, 0.12 },
  { 0.77, 0.28, 0.13, 0.43, 4.40, 0.13, 0.16 },
  { 0.29, 0.67, 0.16, 0.57, 5.10, 0.16, 0.14 },
  { 0.51, 0.72, 0.19, 0.46, 2.55, 0.18, 0.13 },
  { 0.74, 0.67, 0.15, 0.68, 0.90, 0.15, 0.17 },
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
  gdouble angle = phase * blob->speed + blob->phase;

  *x_out = blob->x +
           blob->x_scale * sin (angle) +
           blob->x_scale * 0.35 * sin (angle * 0.47 + blob->phase);
  *y_out = blob->y +
           blob->y_scale * cos (angle * 0.83) +
           blob->y_scale * 0.30 * cos (angle * 0.39 + blob->phase);
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

              dx = (normalized_x - blob_x) * aspect;
              dy = normalized_y - blob_y;
              distance = (dx * dx + dy * dy) / (radius * radius);
              field += 0.20 / (distance + 0.045);

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

          alpha = flow_smoothstep (0.72, 1.20, field);
          if (alpha <= 0.001)
            continue;

          alpha *= dark ? 0.68 : 0.56;
          specular = CLAMP (specular * 0.16, 0.0, 0.30);

          red = base_red + specular;
          green = base_green + specular * 1.10;
          blue = base_blue + specular * 1.25;

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
