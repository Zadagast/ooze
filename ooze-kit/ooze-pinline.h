#pragma once

#include "ooze-draw.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Thin Aqua pinline divider for layouts (e.g. Spot Columns).
 * Vertical sides are 2px wide; horizontal sides are 2px tall.
 * Non-interactive — paints ooze_draw_separator() from the live palette.
 */
GtkWidget *ooze_pinline_new (OozeSide side);

G_END_DECLS
