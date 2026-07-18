#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

G_BEGIN_DECLS

char *ooze_wallpaper_select_uri (const char *picture_uri,
                                  const char *picture_uri_dark,
                                  gboolean    dark);

GdkPixbuf *ooze_wallpaper_load_pixbuf (const char *uri,
                                       int         width,
                                       int         height);

G_END_DECLS
