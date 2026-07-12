#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Size a GtkPopoverMenu (or any popover with an inner scrolled window) to
 * use available monitor space. Scrollbars appear only when the menu is
 * taller than the screen allows — not at GTK's default ~300px cap.
 */
void ooze_popover_fit_screen (GtkPopover *popover);

G_END_DECLS
