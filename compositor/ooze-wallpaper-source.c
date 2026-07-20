#include "ooze-wallpaper-source.h"

#include <gio/gio.h>
#include <math.h>

char *
ooze_wallpaper_select_uri (const char *picture_uri,
                           const char *picture_uri_dark,
                           gboolean    dark)
{
  const char *uri = dark && picture_uri_dark && picture_uri_dark[0]
    ? picture_uri_dark
    : picture_uri;

  if (!uri || !uri[0])
    return NULL;

  return g_strdup (uri);
}

GdkPixbuf *
ooze_wallpaper_load_pixbuf (const char *uri,
                            int         width,
                            int         height)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *path = NULL;
  GdkPixbuf *source;
  GdkPixbuf *scaled_source;
  GdkPixbuf *scaled;
  double scale;
  int scaled_width;
  int scaled_height;
  int crop_x;
  int crop_y;

  if (!uri || !uri[0] || width < 1 || height < 1)
    return NULL;

  file = g_file_new_for_uri (uri);
  if (g_file_is_native (file))
    {
      path = g_file_get_path (file);
      if (!path)
        return NULL;
      source = gdk_pixbuf_new_from_file (path, &error);
    }
  else
    return NULL;

  if (!source)
    return NULL;

  scale = MAX ((double) width / gdk_pixbuf_get_width (source),
               (double) height / gdk_pixbuf_get_height (source));
  scaled_width = MAX (width,
                      (int) ceil (gdk_pixbuf_get_width (source) * scale));
  scaled_height = MAX (height,
                       (int) ceil (gdk_pixbuf_get_height (source) * scale));
  scaled_source = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                   TRUE,
                                   8,
                                   scaled_width,
                                   scaled_height);
  if (!scaled_source)
    {
      g_object_unref (source);
      return NULL;
    }

  gdk_pixbuf_scale (source,
                    scaled_source,
                    0,
                    0,
                    scaled_width,
                    scaled_height,
                    0,
                    0,
                    scale,
                    scale,
                    GDK_INTERP_BILINEAR);
  crop_x = (scaled_width - width) / 2;
  crop_y = (scaled_height - height) / 2;
  scaled = gdk_pixbuf_new_subpixbuf (scaled_source,
                                     crop_x,
                                     crop_y,
                                     width,
                                     height);
  g_object_unref (scaled_source);
  g_object_unref (source);
  return scaled;
}
