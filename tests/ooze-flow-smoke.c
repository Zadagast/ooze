#include "../compositor/ooze-flow.h"

#include <cairo/cairo.h>
#include <glib.h>
#include <stdlib.h>
#include <sys/stat.h>

static void
prepare_test_environment (void)
{
  g_autofree char *runtime_dir = g_dir_make_tmp ("ooze-flow-runtime-XXXXXX",
                                                  NULL);

  g_assert_nonnull (runtime_dir);
  g_assert_cmpint (chmod (runtime_dir, 0700), ==, 0);
  g_assert_true (g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE));
  g_assert_true (g_setenv ("GTK_A11Y", "none", TRUE));
  g_assert_true (g_setenv ("GSETTINGS_BACKEND", "memory", TRUE));
  g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");
}

static guint
count_nonuniform_pixels (cairo_surface_t *surface)
{
  unsigned char *data;
  unsigned char *first;
  int width;
  int height;
  int stride;
  guint changed = 0;
  int x;
  int y;

  cairo_surface_flush (surface);
  data = cairo_image_surface_get_data (surface);
  width = cairo_image_surface_get_width (surface);
  height = cairo_image_surface_get_height (surface);
  stride = cairo_image_surface_get_stride (surface);
  first = data;

  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        unsigned char *pixel = data + y * stride + x * 4;
        guint distance = 0;
        guint channel;

        for (channel = 0; channel < 4; channel++)
          distance += abs ((int) pixel[channel] - (int) first[channel]);
        if (distance > 12)
          changed++;
      }

  return changed;
}

static guint
count_frame_differences (cairo_surface_t *first,
                         cairo_surface_t *second)
{
  unsigned char *first_data;
  unsigned char *second_data;
  int width;
  int height;
  int first_stride;
  int second_stride;
  guint changed = 0;
  int x;
  int y;

  cairo_surface_flush (first);
  cairo_surface_flush (second);
  first_data = cairo_image_surface_get_data (first);
  second_data = cairo_image_surface_get_data (second);
  width = cairo_image_surface_get_width (first);
  height = cairo_image_surface_get_height (first);
  first_stride = cairo_image_surface_get_stride (first);
  second_stride = cairo_image_surface_get_stride (second);

  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        unsigned char *a = first_data + y * first_stride + x * 4;
        unsigned char *b = second_data + y * second_stride + x * 4;
        guint distance = 0;
        guint channel;

        for (channel = 0; channel < 4; channel++)
          distance += abs ((int) a[channel] - (int) b[channel]);
        if (distance > 12)
          changed++;
      }

  return changed;
}

static void
test_flow_smoke (void)
{
  cairo_surface_t *first;
  cairo_surface_t *second;

  first = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 640, 360);
  second = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 640, 360);
  g_assert_cmpint (cairo_surface_status (first), ==, CAIRO_STATUS_SUCCESS);
  g_assert_cmpint (cairo_surface_status (second), ==, CAIRO_STATUS_SUCCESS);

  ooze_flow_render (first, 640, 360, 0.0, FALSE);
  ooze_flow_render (second, 640, 360, 1.0, FALSE);

  g_assert_cmpuint (count_nonuniform_pixels (first), >, 100);
  g_assert_cmpuint (count_frame_differences (first, second), >, 100);

  cairo_surface_destroy (first);
  cairo_surface_destroy (second);
}

int
main (int argc, char **argv)
{
  prepare_test_environment ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/screensaver/flow", test_flow_smoke);
  return g_test_run ();
}
