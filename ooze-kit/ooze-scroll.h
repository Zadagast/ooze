#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeKit Aqua sliding-window scrollbars.
 *
 * GTK already sizes the thumb from GtkAdjustment:page-size (visible fraction
 * of the document). Overlay scrolling hides that map — these helpers keep a
 * classic always-visible trough + raised glass slider.
 */

void       ooze_scroll_ensure_css (void);

/* Scrolled window with overlay scrolling off and .ooze-scrolled class. */
GtkWidget *ooze_scrolled_window_new (void);

G_END_DECLS
