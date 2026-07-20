#pragma once

#include <cairo/cairo.h>
#include <glib.h>

G_BEGIN_DECLS

void ooze_flow_render (cairo_surface_t *surface,
                       int              width,
                       int              height,
                       gdouble          phase,
                       gboolean         dark);

void ooze_flow_render_scene (cairo_surface_t *surface,
                             int              width,
                             int              height,
                             gdouble          phase,
                             gboolean         dark,
                             const char      *scene);

void ooze_flow_render_with_color (cairo_surface_t *surface,
                                  int              width,
                                  int              height,
                                  gdouble          phase,
                                  gboolean         dark,
                                  gdouble          red,
                                  gdouble          green,
                                  gdouble          blue);

G_END_DECLS
