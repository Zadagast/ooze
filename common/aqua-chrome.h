#pragma once

#include <glib.h>
#include <cairo.h>

G_BEGIN_DECLS

/* Shared chrome strip height — title bar and status bar stay matched. */
#define AQUA_TITLEBAR_HEIGHT      28
#define AQUA_STATUSBAR_HEIGHT     AQUA_TITLEBAR_HEIGHT
#define AQUA_TRAFFIC_LIGHT_SIZE   14
#define AQUA_TRAFFIC_LIGHT_GAP    8
#define AQUA_TRAFFIC_LIGHT_MARGIN 10

#define AQUA_TRAFFIC_CLOSE_R 1.0
#define AQUA_TRAFFIC_CLOSE_G 0.373
#define AQUA_TRAFFIC_CLOSE_B 0.337

#define AQUA_TRAFFIC_MINIMIZE_R 1.0
#define AQUA_TRAFFIC_MINIMIZE_G 0.737
#define AQUA_TRAFFIC_MINIMIZE_B 0.180

#define AQUA_TRAFFIC_ZOOM_R 0.153
#define AQUA_TRAFFIC_ZOOM_G 0.788
#define AQUA_TRAFFIC_ZOOM_B 0.251

void aqua_traffic_draw_circle (cairo_t *cr,
                               gdouble  cx,
                               gdouble  cy,
                               gdouble  size,
                               gdouble  r,
                               gdouble  g,
                               gdouble  b);

G_END_DECLS
