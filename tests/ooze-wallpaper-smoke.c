#include "../compositor/ooze-wallpaper-source.h"

#include <glib.h>
#include <glib/gstdio.h>

static void
test_empty_uri_uses_aqua (void)
{
  g_assert_null (ooze_wallpaper_select_uri ("", "", FALSE));
  g_assert_null (ooze_wallpaper_load_pixbuf ("file:///does/not/exist.png",
                                             64,
                                             64));
}

static void
test_theme_uri_selection (void)
{
  g_autofree char *light = NULL;
  g_autofree char *dark = NULL;

  light = ooze_wallpaper_select_uri ("file:///light.png",
                                     "file:///dark.png",
                                     FALSE);
  dark = ooze_wallpaper_select_uri ("file:///light.png",
                                    "file:///dark.png",
                                    TRUE);

  g_assert_cmpstr (light, ==, "file:///light.png");
  g_assert_cmpstr (dark, ==, "file:///dark.png");
}

static void
test_valid_image_loads (void)
{
  g_autofree char *path = NULL;
  g_autofree char *uri = NULL;
  g_autoptr (GdkPixbuf) source = NULL;
  g_autoptr (GdkPixbuf) loaded = NULL;
  g_autoptr (GError) error = NULL;

  source = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 2, 1);
  g_assert_nonnull (source);
  gdk_pixbuf_fill (source, 0x3366ccff);

  path = g_build_filename (g_get_tmp_dir (), "ooze-wallpaper-smoke.png", NULL);
  g_assert_true (gdk_pixbuf_save (source, path, "png", &error, NULL));
  uri = g_filename_to_uri (path, NULL, NULL);
  loaded = ooze_wallpaper_load_pixbuf (uri, 32, 16);

  g_assert_nonnull (loaded);
  g_assert_cmpint (gdk_pixbuf_get_width (loaded), ==, 32);
  g_assert_cmpint (gdk_pixbuf_get_height (loaded), ==, 16);
  g_remove (path);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/wallpaper/empty-uri", test_empty_uri_uses_aqua);
  g_test_add_func ("/wallpaper/theme-selection", test_theme_uri_selection);
  g_test_add_func ("/wallpaper/valid-image", test_valid_image_loads);
  return g_test_run ();
}
