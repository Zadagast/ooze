#include "ooze-about-window.h"
#include "ooze-command-window.h"
#include "ooze-defaults-window.h"
#include "ooze-eye-window.h"
#include "ooze-init.h"
#include "ooze-king-window.h"
#include "ooze-launch-window.h"
#include "ooze-monitor-window.h"
#include "ooze-pak-window.h"
#include "ooze-shot-window.h"
#include "ooze-scenery-window.h"
#include "ooze-surface.h"
#include "spot-window.h"
#include "ooze-themes-window.h"

#include <gsk/gskcairorenderer.h>
#include <gtk/gtk.h>

#include <math.h>
#include <sys/stat.h>

typedef GtkWidget *(*AppWindowConstructor) (GtkApplication *app);

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

  root = g_dir_make_tmp ("ooze-app-smoke-XXXXXX", NULL);
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

  runtime_dir = g_dir_make_tmp ("ooze-app-runtime-XXXXXX", NULL);
  g_assert_nonnull (runtime_dir);
  g_assert_cmpint (chmod (runtime_dir, 0700), ==, 0);
  g_assert_true (g_setenv ("XDG_RUNTIME_DIR", runtime_dir, TRUE));
  g_assert_true (g_setenv ("GTK_A11Y", "none", TRUE));
  g_assert_true (g_setenv ("GSETTINGS_BACKEND", "memory", TRUE));
  g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");

  prepare_test_icon_theme ();
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
assert_palette_resolves (GtkWidget *widget)
{
  GdkRGBA background;
  GdkRGBA foreground;
  GtkCssProvider *provider;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    ".ooze-app-smoke-background { color: @window_bg_color; }"
    ".ooze-app-smoke-foreground { color: @window_fg_color; }");
  gtk_style_context_add_provider_for_display (
    gtk_widget_get_display (widget),
    GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  gtk_widget_add_css_class (widget, "ooze-app-smoke-background");
  gtk_widget_get_color (widget, &background);
  gtk_widget_remove_css_class (widget, "ooze-app-smoke-background");
  gtk_widget_add_css_class (widget, "ooze-app-smoke-foreground");
  gtk_widget_get_color (widget, &foreground);
  gtk_widget_remove_css_class (widget, "ooze-app-smoke-foreground");

  g_assert_cmpfloat (color_distance (&background, &foreground), >, 0.1);
}

static void
assert_window_renders (GtkApplication         *app,
                       AppWindowConstructor    constructor,
                       const char             *name)
{
  g_autoptr (GtkWindow) window = NULL;
  g_autoptr (GskRenderer) renderer = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  GtkWidget *child;
  GtkSnapshot *snapshot;
  GskRenderNode *node;
  GdkSurface *native_surface;
  graphene_rect_t viewport;
  guint width;
  guint height;
  guint changed;

  window = GTK_WINDOW (constructor (app));
  g_assert_nonnull (window);
  gtk_window_present (window);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (gtk_widget_get_realized (GTK_WIDGET (window)));
  child = gtk_window_get_child (window);
  g_assert_nonnull (child);
  g_assert_cmpint (gtk_widget_get_width (child), >, 0);
  g_assert_cmpint (gtk_widget_get_height (child), >, 0);
  assert_palette_resolves (child);

  native_surface = gtk_native_get_surface (
    GTK_NATIVE (gtk_widget_get_native (GTK_WIDGET (window))));
  g_assert_nonnull (native_surface);

  renderer = gsk_cairo_renderer_new ();
  g_assert_true (gsk_renderer_realize (renderer, native_surface, &error));
  g_assert_no_error (error);

  snapshot = gtk_snapshot_new ();
  gtk_widget_snapshot_child (GTK_WIDGET (window), child, snapshot);
  node = gtk_snapshot_free_to_node (snapshot);
  g_assert_nonnull (node);

  width = gtk_widget_get_width (child);
  height = gtk_widget_get_height (child);
  graphene_rect_init (&viewport, 0, 0, width, height);
  texture = gsk_renderer_render_texture (renderer, node, &viewport);
  gsk_render_node_unref (node);
  g_assert_nonnull (texture);

  changed = count_changed_pixels (texture, width, height);
  g_assert_cmpuint (changed, >, 100);

  gsk_renderer_unrealize (renderer);
  gtk_window_destroy (window);
  window = NULL;
  g_test_message ("Rendered %s (%ux%u, %u changed pixels)",
                  name, width, height, changed);
}

static GtkWidget *
new_spot_window (GtkApplication *app)
{
  return GTK_WIDGET (spot_window_new (app));
}

static GtkWidget *
new_command_window (GtkApplication *app)
{
  return ooze_command_window_new (app);
}

static GtkWidget *
new_king_window (GtkApplication *app)
{
  return ooze_king_window_new (app);
}

static GtkWidget *
new_launch_window (GtkApplication *app)
{
  return ooze_launch_window_new (app);
}

static GtkWidget *
new_eye_window (GtkApplication *app)
{
  return ooze_eye_window_new (app);
}

static GtkWidget *
new_shot_window (GtkApplication *app)
{
  return ooze_shot_window_new (app);
}

static GtkWidget *
new_about_window (GtkApplication *app)
{
  return ooze_about_window_new (app);
}

static GtkWidget *
new_defaults_window (GtkApplication *app)
{
  return ooze_defaults_window_new (app);
}

static GtkWidget *
new_monitor_window (GtkApplication *app)
{
  return ooze_monitor_window_new (app);
}

static GtkWidget *
new_themes_window (GtkApplication *app)
{
  return ooze_themes_window_new (app);
}

static GtkWidget *
new_pak_window (GtkApplication *app)
{
  return GTK_WIDGET (ooze_pak_window_new (app));
}

static GtkWidget *
new_scenery_window (GtkApplication *app)
{
  return ooze_scenery_window_new (app);
}

static GtkApplication *test_app;

static void
test_spot (void)
{
  assert_window_renders (test_app, new_spot_window, "Spot");
}

static void
test_command (void)
{
  assert_window_renders (test_app, new_command_window, "Ooze Command");
}

static void
test_king (void)
{
  assert_window_renders (test_app, new_king_window, "Ooze King");
}

static void
test_launch (void)
{
  assert_window_renders (test_app, new_launch_window, "Ooze Launch");
}

static void
test_eye (void)
{
  assert_window_renders (test_app, new_eye_window, "Ooze Eye");
}

static void
test_shot (void)
{
  assert_window_renders (test_app, new_shot_window, "Ooze Shot");
}

static void
test_about (void)
{
  assert_window_renders (test_app, new_about_window, "Ooze About");
}

static void
test_defaults (void)
{
  assert_window_renders (test_app, new_defaults_window, "Ooze Defaults");
}

static void
test_monitor (void)
{
  assert_window_renders (test_app, new_monitor_window, "Ooze Monitor");
}

static void
test_themes (void)
{
  assert_window_renders (test_app, new_themes_window, "Ooze Themes");
}

static void
test_pak (void)
{
  assert_window_renders (test_app, new_pak_window, "Ooze Pak");
}

static void
test_scenery (void)
{
  assert_window_renders (test_app, new_scenery_window, "Ooze Scenery");
}

int
main (int argc, char **argv)
{
  prepare_test_environment ();
  g_test_init (&argc, &argv, NULL);
  ooze_kit_init ();
  test_app = gtk_application_new ("org.ooze.AppSmoke",
                                  G_APPLICATION_NON_UNIQUE);
  {
    g_autoptr (GError) error = NULL;

    g_assert_true (g_application_register (G_APPLICATION (test_app),
                                           NULL,
                                           &error));
    g_assert_no_error (error);
  }

  g_test_add_func ("/app/spot", test_spot);
  g_test_add_func ("/app/command", test_command);
  g_test_add_func ("/app/king", test_king);
  g_test_add_func ("/app/launch", test_launch);
  g_test_add_func ("/app/eye", test_eye);
  g_test_add_func ("/app/shot", test_shot);
  g_test_add_func ("/app/about", test_about);
  g_test_add_func ("/app/defaults", test_defaults);
  g_test_add_func ("/app/monitor", test_monitor);
  g_test_add_func ("/app/themes", test_themes);
  g_test_add_func ("/app/pak", test_pak);
  g_test_add_func ("/app/scenery", test_scenery);

  {
    int status = g_test_run ();
    g_clear_object (&test_app);
    return status;
  }
}
