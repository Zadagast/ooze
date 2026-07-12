#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Present an About dialog with the Ooze brand name as the application name.
 * Friendly launcher names stay elsewhere; About keeps the product identity.
 */
void ooze_about_present (GtkWindow  *parent,
                         const char *brand_name,
                         const char *icon_name,
                         const char *comments);

G_END_DECLS
