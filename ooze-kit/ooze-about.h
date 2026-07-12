#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Present a small Ooze Gel About window for the given app brand.
 * Friendly launcher names stay elsewhere; About keeps the product identity.
 * version may be NULL to omit the version line.
 */
void ooze_about_present (GtkWindow  *parent,
                         const char *brand_name,
                         const char *icon_name,
                         const char *comments,
                         const char *version);

G_END_DECLS
