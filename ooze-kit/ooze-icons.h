#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Canonical OozeKit icon pixel sizes. All chrome / list / grid icons should
 * use these so hit-boxes and rendered glyphs stay consistent.
 *
 *   TOOLBAR  – Spot / Settings labeled buttons (icon above caption)
 *   SIDEBAR  – place lists next to labels
 *   LIST     – column / compact rows
 *   GRID     – icon-view folder / file tiles
 */
#define OOZE_ICON_SIZE_TOOLBAR 40
#define OOZE_ICON_SIZE_SIDEBAR 24
#define OOZE_ICON_SIZE_LIST    16
#define OOZE_ICON_SIZE_GRID    48

/*
 * Load a theme icon into a GtkImage with a fixed square viewport.
 * Prefers full-color icons; falls back to symbolic only when needed.
 * Always sets pixel size + size-request so mixed SVG assets align.
 */
GtkWidget *ooze_icon_image_new (const char * const *icon_names,
                                int                 icon_px);

/* Same lookup used by ooze_icon_image_new / OozeButton (caller owns ref). */
GtkIconPaintable *ooze_icon_lookup (GtkIconTheme         *theme,
                                    const char * const   *icon_names,
                                    int                   icon_px);

G_END_DECLS
