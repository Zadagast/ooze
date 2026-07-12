#pragma once

#include <glib.h>
#include <cairo.h>

G_BEGIN_DECLS

/*
 * Ooze Gel pinline grid
 * ─────────────────────
 * Strip heights are multiples of OOZE_PIN_STRIDE so pinlines flow as one
 * cloth across the Ooze Gel title bar and matching OozeKit surfaces
 * (MAIN BAR, sidebar, status bar). Do not invent off-grid heights.
 */
#define OOZE_PIN_STRIDE           4

#define AQUA_TITLEBAR_HEIGHT      32   /* 8 × OOZE_PIN_STRIDE — Ooze Gel title */
#define AQUA_STATUSBAR_HEIGHT     AQUA_TITLEBAR_HEIGHT

/* Nominal MAIN BAR height (documentation only — NOT enforced via
 * size_request/CSS). The strip sizes to its tallest tile's real content;
 * this is just the typical result with 40px icons + caption + kit pad. */
#define OOZE_TOOLBAR_HEIGHT       96

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
