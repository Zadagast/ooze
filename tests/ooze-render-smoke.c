#include "ooze-button.h"
#include "ooze-init.h"
#include "ooze-surface.h"

#include <gsk/gskcairorenderer.h>
#include <gtk/gtk.h>

#include <math.h>
#include <sys/stat.h>

static void prepare_test_environment (void);

static double
color_distance (const GdkRGBA *a,
                 const GdkRGBA *b)
{
  double dr = a->red - b->red;
  double dg = a->green - b->green;
  double db = a->blue - b->blue;

  return sqrt (dr * dr + dg * dg + db * db);
}

static void
prepare_test_icon_theme (void)
{
  g_autofree char *root = NULL;
  g_autofree char *theme = NULL;
  g_autofree char *actions = NULL;
  g_autofree char *index = NULL;

  root = g_dir_make_tmp ("ooze-render-smoke-XXXXXX", NULL);
  g_assert_nonnull (root);
  theme = g_build_filename (root, "icons", "elementary", NULL);
  actions = g_build_filename (theme, "actions", NULL);
  g_assert_cmpint (g_mkdir_with_parents (actions, 0700), ==, 0);
  index = g_build_filename (theme, "index.theme", NULL);
  g_assert_true (g_file_set_contents (
    index,
    "[Icon Theme]\n"
    "Name=elementary\n"
    "Directories=actions\n"
    "Inherits=Adwaita\n",
    -1,
    NULL));
  g_setenv ("OOZE_DATA_DIR", root, TRUE);
}

static void
prepare_test_environment (void)
{
  g_autofree char *runtime_dir = NULL;

  runtime_dir = g_dir_make_tmp ("ooze-render-runtime-XXXXXX", NULL);
  g_assert_nonnull (runtime_dir);
  g_assert_cmpint (chmod (runtime_dir, 0700), ==, 0);
  g_assert_true (g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE));
  g_assert_true (g_setenv ("GTK_A11Y", "none", TRUE));
  g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");

  prepare_test_icon_theme ();
}

static void
add_color_probe_css (GdkDisplay *display)
{
  GtkCssProvider *provider;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    ".ooze-render-smoke-background { color: @window_bg_color; }"
    ".ooze-render-smoke-foreground { color: @window_fg_color; }");
  gtk_style_context_add_provider_for_display (
    display,
    GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
}

static guint
count_changed_pixels (GdkTexture *texture,
                      guint       width,
                      guint       height)
{
  gsize stride = width * 4;
  gsize size = stride * height;
  g_autofree guint8 *pixels = g_malloc (size);
  guint8 *first;
  guint changed = 0;
  guint x;
  guint y;

  gdk_texture_download (texture, pixels, stride);
  first = pixels;

  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        guint8 *pixel = pixels + y * stride + x * 4;
        guint distance = 0;
        guint channel;

        for (channel = 0; channel < 3; channel++)
          distance += abs ((int) pixel[channel] - (int) first[channel]);
        if (distance > 12)
          changed++;
      }

  return changed;
}

static void
test_render_smoke (void)
{
  static const char * const icon_names[] = {
    "view-refresh-symbolic",
    "view-refresh",
    NULL,
  };
  g_autoptr (GtkWindow) window = NULL;
  GtkWidget *surface;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *check;
  GtkWidget *background_probe;
  GtkWidget *foreground_probe;
  g_autoptr (GskRenderer) renderer = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  GdkDisplay *display;
  GdkRGBA background;
  GdkRGBA foreground;
  GtkSnapshot *snapshot;
  GskRenderNode *node;
  GdkSurface *native_surface;
  graphene_rect_t viewport;
  guint width;
  guint height;
  guint changed;

  ooze_kit_init ();
  display = gdk_display_get_default ();
  g_assert_nonnull (display);
  add_color_probe_css (display);

  window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_default_size (window, 480, 180);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR,
                              GTK_ORIENTATION_HORIZONTAL);
  label = gtk_label_new ("OozeKit render smoke");
  button = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR,
                                    icon_names,
                                    32,
                                    "Refresh",
                                    "Refresh");
  check = gtk_check_button_new_with_label ("Ready");
  background_probe = gtk_label_new ("");
  foreground_probe = gtk_label_new ("");
  gtk_widget_add_css_class (background_probe,
                            "ooze-render-smoke-background");
  gtk_widget_add_css_class (foreground_probe,
                            "ooze-render-smoke-foreground");
  gtk_widget_set_opacity (background_probe, 0.0);
  gtk_widget_set_opacity (foreground_probe, 0.0);

  gtk_box_append (GTK_BOX (surface), label);
  gtk_box_append (GTK_BOX (surface), button);
  gtk_box_append (GTK_BOX (surface), check);
  gtk_box_append (GTK_BOX (surface), background_probe);
  gtk_box_append (GTK_BOX (surface), foreground_probe);
  gtk_window_set_child (window, surface);
  gtk_window_present (window);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (gtk_widget_get_realized (GTK_WIDGET (window)));
  g_assert_cmpint (gtk_widget_get_width (surface), >, 0);
  g_assert_cmpint (gtk_widget_get_height (surface), >, 0);

  gtk_widget_get_color (background_probe, &background);
  gtk_widget_get_color (foreground_probe, &foreground);
  g_assert_cmpfloat (color_distance (&background, &foreground), >, 0.1);

  native_surface = gtk_native_get_surface (
    GTK_NATIVE (gtk_widget_get_native (GTK_WIDGET (window))));
  g_assert_nonnull (native_surface);

  renderer = gsk_cairo_renderer_new ();
  g_assert_true (gsk_renderer_realize (renderer, native_surface, &error));
  g_assert_no_error (error);

  snapshot = gtk_snapshot_new ();
  gtk_widget_snapshot_child (GTK_WIDGET (window), surface, snapshot);
  node = gtk_snapshot_free_to_node (snapshot);
  g_assert_nonnull (node);

  width = gtk_widget_get_width (surface);
  height = gtk_widget_get_height (surface);
  graphene_rect_init (&viewport, 0, 0, width, height);
  texture = gsk_renderer_render_texture (renderer, node, &viewport);
  gsk_render_node_unref (node);
  g_assert_nonnull (texture);

  changed = count_changed_pixels (texture, width, height);
  g_assert_cmpuint (changed, >, 100);

  gsk_renderer_unrealize (renderer);
  gtk_window_destroy (window);
  window = NULL;
}

int
main (int argc, char **argv)
{
  prepare_test_environment ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/render/smoke", test_render_smoke);
  return g_test_run ();
}
