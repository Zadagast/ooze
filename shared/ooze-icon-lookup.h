#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

/* Freedesktop icon lookup for the compositor (no GtkIconTheme). */

GdkPixbuf *ooze_icon_lookup_load (const char *icon_name,
                                  int         size);

/* Prefer themed names from GThemedIcon; load file path from GFileIcon. */
GdkPixbuf *ooze_icon_lookup_from_gicon (GIcon *icon,
                                        int    size);

/* Try each name until one loads. */
GdkPixbuf *ooze_icon_lookup_first (const char * const *icon_names,
                                   int                 size);

/* Installed icon themes with index.theme (Directories=). Caller frees. */
char **ooze_icon_lookup_list_icon_themes (void);

/* Installed cursor themes (cursors/ dir or index.theme). Caller frees. */
char **ooze_icon_lookup_list_cursor_themes (void);

gboolean ooze_icon_lookup_theme_installed (const char *theme_name);

G_END_DECLS
