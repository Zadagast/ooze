#include "my-dock.h"
#include "my-aqua-draw.h"

#define __COGL_H_INSIDE__
#include "cogl/cogl-texture-2d.h"
#undef __COGL_H_INSIDE__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gdesktopappinfo.h>
#include <png.h>

#include <errno.h>
#include <stdio.h>

#include <meta/meta-background-content.h>
#include <meta/meta-background.h>
#include <meta/meta-wayland-client.h>
#include <meta/window.h>

#define DOCK_HEIGHT       48.0f
#define LAUNCHER_SIZE     40.0f
#define ICON_SIZE         32
#define LAUNCHER_SPACING  8.0f

typedef struct
{
  const char *desktop_id;
  const char *executable;
} DockAppEntry;

struct _MyDock
{
  MetaContext *context;
  MetaDisplay *display;
  ClutterActor *actor;
};

static const DockAppEntry dock_apps[] = {
  { "org.gnome.Ptyxis.desktop", "ptyxis" },
  { "org.gnome.TextEditor.desktop", "gnome-text-editor" },
  { "org.gnome.Calculator.desktop", "gnome-calculator" },
};

void
my_dock_launch_spot (void)
{
  g_spawn_command_line_async ("spot", NULL);
}

static gboolean
on_spot_launcher_pressed (ClutterActor *actor G_GNUC_UNUSED,
                          ClutterEvent *event,
                          gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  my_dock_launch_spot ();
  return CLUTTER_EVENT_STOP;
}

ClutterActor *
my_dock_create_spot_launcher (ClutterActor *stage,
                            MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_aqua_dock_icon_content (stage,
                                       texture,
                                       0.22f,
                                       0.48f,
                                       0.92f);
  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical,
                                      logical,
                                      texture,
                                      texture);

  g_signal_connect (button,
                    "button-press-event",
                    G_CALLBACK (on_spot_launcher_pressed),
                    NULL);

  return button;
}

static void
my_dock_free_search_results (gchar ***results)
{
  int i;

  if (!results)
    return;

  for (i = 0; results[i] != NULL; i++)
    g_strfreev (results[i]);
  g_free (results);
}

static GDesktopAppInfo *
my_dock_find_app_info (const DockAppEntry *entry)
{
  GDesktopAppInfo *info = NULL;
  GList *all;
  GList *l;

  if (entry->desktop_id)
    {
      info = g_desktop_app_info_new (entry->desktop_id);
      if (info)
        return info;
    }

  if (entry->executable)
    {
      gchar ***results;

      results = g_desktop_app_info_search (entry->executable);
      if (results)
        {
          if (results[0] && results[0][0])
            info = g_desktop_app_info_new (results[0][0]);
          my_dock_free_search_results (results);
          if (info)
            return info;
        }

      all = g_app_info_get_all ();
      for (l = all; l != NULL; l = l->next)
        {
          GAppInfo *app = l->data;
          const char *exec;

          if (!G_IS_DESKTOP_APP_INFO (app))
            continue;

          exec = g_app_info_get_executable (app);
          if (exec &&
              g_strcmp0 (g_path_get_basename (exec), entry->executable) == 0)
            return g_object_ref (G_DESKTOP_APP_INFO (app));
        }

      g_list_free (all);
    }

  return NULL;
}

static gboolean
my_dock_window_matches_app (MetaWindow       *window,
                            GDesktopAppInfo  *app_info)
{
  const char *wm_class;
  const char *wm_instance;
  const char *gtk_app_id;
  const char *startup_wm_class;
  const char *exec;
  g_autofree char *desktop_id = NULL;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return FALSE;

  if (meta_window_get_transient_for (window) != NULL)
    return FALSE;

  wm_class = meta_window_get_wm_class (window);
  wm_instance = meta_window_get_wm_class_instance (window);
  gtk_app_id = meta_window_get_gtk_application_id (window);
  startup_wm_class = g_desktop_app_info_get_startup_wm_class (app_info);
  exec = g_app_info_get_executable (G_APP_INFO (app_info));
  if (g_str_has_suffix (g_app_info_get_id (G_APP_INFO (app_info)), ".desktop"))
    desktop_id = g_strndup (g_app_info_get_id (G_APP_INFO (app_info)),
                            strlen (g_app_info_get_id (G_APP_INFO (app_info))) - 8);

  if (startup_wm_class &&
      ((wm_class && g_strcmp0 (startup_wm_class, wm_class) == 0) ||
       (wm_instance && g_strcmp0 (startup_wm_class, wm_instance) == 0)))
    return TRUE;

  if (gtk_app_id && desktop_id && g_strcmp0 (gtk_app_id, desktop_id) == 0)
    return TRUE;

  if (exec && wm_instance &&
      g_ascii_strcasecmp (g_path_get_basename (exec), wm_instance) == 0)
    return TRUE;

  return FALSE;
}

static GList *
my_dock_get_app_windows (MyDock           *dock,
                         GDesktopAppInfo  *app_info)
{
  GList *all_windows;
  GList *l;
  GList *matches = NULL;

  all_windows = meta_display_list_all_windows (dock->display);
  for (l = all_windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (my_dock_window_matches_app (window, app_info))
        matches = g_list_prepend (matches, window);
    }

  g_list_free (all_windows);
  return g_list_reverse (matches);
}

static gboolean
my_dock_window_is_visible (MetaWindow *window)
{
  return meta_window_showing_on_its_workspace (window) &&
         !meta_window_is_hidden (window);
}

static MetaWindow *
my_dock_get_most_recent_window (GList *windows)
{
  MetaWindow *best = NULL;
  guint32 best_time = 0;
  GList *l;

  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      guint32 user_time = meta_window_get_user_time (window);

      if (!best || user_time >= best_time)
        {
          best = window;
          best_time = user_time;
        }
    }

  return best;
}

static void
my_dock_launch_subprocess (MyDock           *dock,
                           GDesktopAppInfo  *app_info)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  const char *executable;
  const char *argv[] = { NULL, NULL };

  executable = g_app_info_get_executable (G_APP_INFO (app_info));
  argv[0] = executable;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);

  if (!meta_wayland_client_new_subprocess (dock->context,
                                           launcher,
                                           argv,
                                           &error))
    g_warning ("Failed to launch %s: %s", executable, error->message);
  else
    g_print ("MyDock: launched %s\n", executable);
}

static void
my_dock_handle_primary_click (MyDock           *dock,
                              GDesktopAppInfo  *app_info)
{
  g_autoptr (GList) windows = NULL;
  GList *l;
  gboolean any_visible = FALSE;
  MetaWindow *focus_window;

  windows = my_dock_get_app_windows (dock, app_info);
  if (!windows)
    {
      my_dock_launch_subprocess (dock, app_info);
      return;
    }

  for (l = windows; l != NULL; l = l->next)
    {
      if (my_dock_window_is_visible (l->data))
        {
          any_visible = TRUE;
          break;
        }
    }

  if (any_visible)
    {
      for (l = windows; l != NULL; l = l->next)
        {
          MetaWindow *window = l->data;

          if (my_dock_window_is_visible (window))
            meta_window_minimize (window);
        }
      return;
    }

  focus_window = my_dock_get_most_recent_window (windows);
  if (focus_window)
    {
      meta_window_unminimize (focus_window);
      meta_window_activate (focus_window, clutter_get_current_event_time ());
    }
}

static CoglTexture *
my_dock_texture_from_pixbuf (ClutterActor *actor,
                             GdkPixbuf    *pixbuf)
{
  ClutterContext *clutter_context;
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  CoglPixelFormat format;
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  int rowstride;

  clutter_context = clutter_actor_get_context (actor);
  backend = clutter_context_get_backend (clutter_context);
  cogl_context = clutter_backend_get_cogl_context (backend);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  format = gdk_pixbuf_get_has_alpha (pixbuf)
    ? COGL_PIXEL_FORMAT_RGBA_8888
    : COGL_PIXEL_FORMAT_RGB_888;

  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width,
                                           height,
                                           format,
                                           rowstride,
                                           gdk_pixbuf_get_pixels (pixbuf),
                                           &error);
  if (!texture)
    g_warning ("MyDock: failed to upload icon texture: %s",
               error ? error->message : "unknown error");

  return texture;
}

static GdkPixbuf *
my_dock_load_png_pixbuf (const char  *path,
                         int          size,
                         GError     **error)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  GdkPixbuf *scaled;
  FILE *fp;
  png_structp png;
  png_infop info;
  png_byte header[8];
  png_uint_32 width;
  png_uint_32 height;
  png_bytep *rows = NULL;
  guchar *pixels;
  int rowstride;
  png_uint_32 png_rowbytes;
  int y;

  fp = fopen (path, "rb");
  if (!fp)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "Failed to open %s: %s",
                   path,
                   g_strerror (errno));
      return NULL;
    }

  if (fread (header, 1, 8, fp) != 8 || png_sig_cmp (header, 0, 8))
    {
      fclose (fp);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                           "Not a PNG file");
      return NULL;
    }

  png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png_create_info_struct (png);
  if (!png || !info)
    {
      fclose (fp);
      png_destroy_read_struct (&png, &info, NULL);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                           "Failed to allocate PNG decoder");
      return NULL;
    }

  if (setjmp (png_jmpbuf (png)))
    {
      g_free (rows);
      png_destroy_read_struct (&png, &info, NULL);
      fclose (fp);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                           "Failed to decode PNG");
      return NULL;
    }

  png_init_io (png, fp);
  png_set_sig_bytes (png, 8);
  png_read_info (png, info);

  width = png_get_image_width (png, info);
  height = png_get_image_height (png, info);

  if (png_get_bit_depth (png, info) == 16)
    png_set_strip_16 (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY &&
      png_get_bit_depth (png, info) < 8)
    png_set_expand_gray_1_2_4_to_8 (png);
  if (png_get_valid (png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_RGB ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_PALETTE)
    png_set_filler (png, 0xff, PNG_FILLER_AFTER);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb (png);

  png_read_update_info (png, info);
  png_rowbytes = png_get_rowbytes (png, info);

  rows = g_new0 (png_bytep, height);
  for (y = 0; y < (int) height; y++)
    rows[y] = g_new (png_byte, png_rowbytes);

  png_read_image (png, rows);
  png_read_end (png, NULL);
  fclose (fp);
  png_destroy_read_struct (&png, &info, NULL);

  pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                           TRUE,
                           8,
                           (int) width,
                           (int) height);
  pixels = gdk_pixbuf_get_pixels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);

  for (y = 0; y < (int) height; y++)
    {
      memcpy (pixels + y * rowstride, rows[y], png_rowbytes);
      g_free (rows[y]);
    }
  g_free (rows);

  if ((int) width != size || (int) height != size)
    {
      scaled = gdk_pixbuf_scale_simple (pixbuf, size, size, GDK_INTERP_BILINEAR);
      if (!scaled)
        {
          g_set_error_literal (error,
                               GDK_PIXBUF_ERROR,
                               GDK_PIXBUF_ERROR_FAILED,
                               "Failed to scale PNG");
          return NULL;
        }
      return scaled;
    }

  return g_steal_pointer (&pixbuf);
}

static GdkPixbuf *
my_dock_try_load_icon_file (const char *path,
                            int         size)
{
  g_autoptr (GError) error = NULL;

  if (g_str_has_suffix (path, ".png"))
    return my_dock_load_png_pixbuf (path, size, &error);

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return NULL;

  return gdk_pixbuf_new_from_file_at_scale (path,
                                            size,
                                            size,
                                            TRUE,
                                            &error);
}

static GdkPixbuf *
my_dock_try_icon_in_dir (const char *dir,
                         const char *icon_name,
                         int         size)
{
  static const char *extensions[] = { ".png", ".svg", ".xpm", NULL };
  gsize e;

  for (e = 0; extensions[e] != NULL; e++)
    {
      g_autofree char *path =
        g_strdup_printf ("%s/%s%s", dir, icon_name, extensions[e]);
      GdkPixbuf *pixbuf = my_dock_try_load_icon_file (path, size);

      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static char *
my_dock_get_icon_theme (void)
{
  g_autoptr (GSettings) settings = NULL;

  settings = g_settings_new ("org.gnome.desktop.interface");
  return g_settings_get_string (settings, "icon-theme");
}

static GdkPixbuf *
my_dock_load_themed_icon (const char *icon_name,
                          int         size)
{
  g_autofree char *user_theme = my_dock_get_icon_theme ();
  const char *themes[] = {
    user_theme,
    "Yaru",
    "Adwaita",
    "gnome",
    "hicolor",
    NULL,
  };
  static const int sizes[] = { 256, 128, 96, 64, 48, 32, 24, 0 };
  gsize t;
  gsize s;

  for (t = 0; themes[t] != NULL; t++)
    {
      g_autofree char *dir = NULL;
      GdkPixbuf *pixbuf;

      if (!themes[t] || themes[t][0] == '\0')
        continue;

      dir = g_strdup_printf ("/usr/share/icons/%s/%dx%d/apps",
                               themes[t],
                               size,
                               size);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return pixbuf;
    }

  for (t = 0; themes[t] != NULL; t++)
    {
      if (!themes[t] || themes[t][0] == '\0')
        continue;

      for (s = 0; sizes[s] != 0; s++)
        {
          g_autofree char *dir = NULL;
          GdkPixbuf *pixbuf;
          int found_w;

          if (sizes[s] == size)
            continue;

          dir = g_strdup_printf ("/usr/share/icons/%s/%dx%d/apps",
                                 themes[t],
                                 sizes[s],
                                 sizes[s]);
          pixbuf = my_dock_try_icon_in_dir (dir, icon_name, sizes[s]);
          if (!pixbuf)
            continue;

          found_w = gdk_pixbuf_get_width (pixbuf);
          if (found_w > size)
            {
              GdkPixbuf *scaled;

              scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                size,
                                                size,
                                                GDK_INTERP_HYPER);
              g_object_unref (pixbuf);
              return scaled;
            }

          return pixbuf;
        }
    }

  for (t = 0; themes[t] != NULL; t++)
    {
      g_autofree char *dir = NULL;
      GdkPixbuf *pixbuf;

      if (!themes[t] || themes[t][0] == '\0')
        continue;

      dir = g_strdup_printf ("/usr/share/icons/%s/scalable/apps", themes[t]);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static GdkPixbuf *
my_dock_load_icon_pixbuf (GDesktopAppInfo  *app_info,
                          int               size)
{
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *icon_name = NULL;
  const char *extensions[] = { ".png", ".svg", ".xpm", NULL };
  gsize e;

  icon = g_app_info_get_icon (G_APP_INFO (app_info));
  if (icon && G_IS_FILE_ICON (icon))
    {
      GFile *file = g_file_icon_get_file (G_FILE_ICON (icon));
      const char *path = g_file_peek_path (file);

      if (path && g_str_has_suffix (path, ".png"))
        return my_dock_load_png_pixbuf (path, size, &error);

      return gdk_pixbuf_new_from_file_at_scale (path,
                                                size,
                                                size,
                                                TRUE,
                                                &error);
    }

  icon_name = g_desktop_app_info_get_string (app_info, "Icon");
  if (!icon_name && icon && G_IS_THEMED_ICON (icon))
    {
      const char * const *names =
        g_themed_icon_get_names (G_THEMED_ICON (icon));

      if (names && names[0])
        icon_name = g_strdup (names[0]);
    }

  if (!icon_name)
    return NULL;

  if (g_path_is_absolute (icon_name))
    {
      if (g_str_has_suffix (icon_name, ".png"))
        return my_dock_load_png_pixbuf (icon_name, size, &error);

      return gdk_pixbuf_new_from_file_at_scale (icon_name,
                                                size,
                                                size,
                                                TRUE,
                                                &error);
    }

  {
    GdkPixbuf *pixbuf = my_dock_load_themed_icon (icon_name, size);

    if (pixbuf)
      return pixbuf;
  }

  for (e = 0; extensions[e] != NULL; e++)
    {
      g_autofree char *path = g_strdup_printf ("/usr/share/pixmaps/%s%s",
                                               icon_name,
                                               extensions[e]);
      GdkPixbuf *pixbuf = my_dock_try_load_icon_file (path, size);

      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static ClutterContent *
my_dock_icon_content (ClutterActor     *actor,
                      MetaDisplay      *display,
                      GDesktopAppInfo  *app_info,
                      int               logical_size)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (CoglTexture) texture = NULL;
  ClutterContent *content;
  int load_size;

  load_size = my_aqua_icon_texture_size (display, logical_size);
  pixbuf = my_dock_load_icon_pixbuf (app_info, load_size);
  if (!pixbuf)
    {
      g_warning ("MyDock: failed to load icon for %s",
                 g_app_info_get_name (G_APP_INFO (app_info)));
      return NULL;
    }

  texture = my_dock_texture_from_pixbuf (actor, pixbuf);
  if (!texture)
    return NULL;

  content = clutter_texture_content_new_from_texture (texture, NULL);

  return content;
}

static gboolean
on_launcher_pressed (ClutterActor *actor,
                     ClutterEvent *event,
                     gpointer      user_data G_GNUC_UNUSED)
{
  GDesktopAppInfo *app_info;
  MetaContext *context;
  MetaDisplay *display;
  MyDock dock;
  guint button;

  button = clutter_event_get_button (event);
  app_info = g_object_get_data (G_OBJECT (actor), "app-info");
  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  display = g_object_get_data (G_OBJECT (actor), "launch-display");
  if (!app_info || !context || !display)
    return CLUTTER_EVENT_PROPAGATE;

  dock.context = context;
  dock.display = display;

  if (button == CLUTTER_BUTTON_MIDDLE)
    {
      my_dock_launch_subprocess (&dock, app_info);
      return CLUTTER_EVENT_STOP;
    }

  if (button == CLUTTER_BUTTON_PRIMARY)
    {
      my_dock_handle_primary_click (&dock, app_info);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
my_dock_layout_launchers (MyDock *dock,
                          gfloat  dock_width)
{
  ClutterActor *launchers;
  ClutterActor *child;
  gsize count = 0;
  gfloat total_width;
  gfloat x;

  launchers = clutter_actor_get_first_child (dock->actor);
  if (!launchers)
    return;

  for (child = clutter_actor_get_first_child (launchers);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    count++;

  if (count == 0)
    return;

  total_width = count * LAUNCHER_SIZE + (count - 1) * LAUNCHER_SPACING;
  x = (dock_width - total_width) / 2.0f;

  for (child = clutter_actor_get_first_child (launchers);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      clutter_actor_set_position (child,
                                  x,
                                  (DOCK_HEIGHT - LAUNCHER_SIZE) / 2.0f);
      x += LAUNCHER_SIZE + LAUNCHER_SPACING;
    }
}

static ClutterActor *
my_dock_create_launcher (MetaContext      *context,
                         MetaDisplay      *display,
                         ClutterActor     *stage,
                         GDesktopAppInfo  *app_info,
                         gboolean          aqua_style)
{
  ClutterActor *button;
  ClutterActor *icon;
  g_autoptr (ClutterContent) content = NULL;
  gfloat button_size = aqua_style ? 48.0f : LAUNCHER_SIZE;
  gfloat icon_size = aqua_style ? 48.0f : ICON_SIZE;
  int texture_size;

  button = clutter_actor_new ();
  clutter_actor_set_size (button, button_size, button_size);
  clutter_actor_set_reactive (button, TRUE);

  if (!aqua_style)
    {
      CoglColor button_color;

      cogl_color_init_from_4f (&button_color, 0.20f, 0.22f, 0.28f, 0.85f);
      clutter_actor_set_background_color (button, &button_color);
    }

  texture_size = my_aqua_icon_texture_size (display, (int) icon_size);
  content = my_dock_icon_content (stage, display, app_info, (int) icon_size);
  if (content)
    {
      icon = clutter_actor_new ();
      my_aqua_actor_set_scaled_content (icon,
                                        g_steal_pointer (&content),
                                        (int) icon_size,
                                        (int) icon_size,
                                        texture_size,
                                        texture_size);
      clutter_actor_add_child (button, icon);
      clutter_actor_set_pivot_point (icon, 0.5f, 0.5f);
      clutter_actor_set_position (icon,
                                  (button_size - icon_size) / 2.0f,
                                  (button_size - icon_size) / 2.0f);
      clutter_actor_show (icon);
    }

  g_object_set_data_full (G_OBJECT (button),
                          "app-info",
                          g_object_ref (app_info),
                          g_object_unref);
  g_object_set_data_full (G_OBJECT (button),
                          "launch-context",
                          g_object_ref (context),
                          g_object_unref);
  g_object_set_data (G_OBJECT (button), "launch-display", display);

  g_signal_connect (button,
                    "button-press-event",
                    G_CALLBACK (on_launcher_pressed),
                    NULL);

  return button;
}

MyDock *
my_dock_new (MetaContext     *context,
             MetaDisplay     *display,
             MetaCompositor  *compositor)
{
  MyDock *dock;
  ClutterActor *stage;
  ClutterActor *window_group;
  ClutterActor *launchers;
  CoglColor dock_color;
  g_autoptr (MetaBackground) background = NULL;
  ClutterContent *content;
  MetaBackgroundContent *background_content;
  gsize i;

  dock = g_new0 (MyDock, 1);
  dock->context = g_object_ref (context);
  dock->display = display;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  window_group = meta_compositor_get_window_group (compositor);

  cogl_color_init_from_4f (&dock_color, 0.11f, 0.12f, 0.16f, 1.0f);

  content = meta_background_content_new (display, 0);
  background_content = META_BACKGROUND_CONTENT (content);
  background = meta_background_new (display);
  meta_background_set_color (background, &dock_color);
  meta_background_content_set_background (background_content, background);

  dock->actor = clutter_actor_new ();
  clutter_actor_set_content (dock->actor, content);
  g_object_unref (content);
  clutter_actor_set_reactive (dock->actor, FALSE);

  launchers = clutter_actor_new ();
  clutter_actor_set_reactive (launchers, FALSE);
  clutter_actor_add_child (dock->actor, launchers);

  clutter_actor_add_child (stage, dock->actor);
  clutter_actor_set_child_above_sibling (stage, dock->actor, window_group);

  for (i = 0; i < G_N_ELEMENTS (dock_apps); i++)
    {
      g_autoptr (GDesktopAppInfo) app_info = NULL;
      const char *executable;

      app_info = my_dock_find_app_info (&dock_apps[i]);
      if (!app_info)
        {
          g_print ("MyDock: no desktop entry for %s\n",
                   dock_apps[i].desktop_id);
          continue;
        }

      executable = g_app_info_get_executable (G_APP_INFO (app_info));
      if (!executable || !g_find_program_in_path (executable))
        {
          g_print ("MyDock: skipping %s (not in PATH)\n",
                   g_app_info_get_name (G_APP_INFO (app_info)));
          continue;
        }

      clutter_actor_add_child (launchers,
                               my_dock_create_launcher (dock->context,
                                                        dock->display,
                                                        stage,
                                                        app_info,
                                                        FALSE));
    }

  clutter_actor_show (dock->actor);

  return dock;
}

void
my_dock_resize (MyDock      *dock,
                MetaDisplay  *display)
{
  int width;
  int height;

  if (!dock || !dock->actor)
    return;

  meta_display_get_size (display, &width, &height);
  clutter_actor_set_size (dock->actor, (gfloat) width, DOCK_HEIGHT);
  clutter_actor_set_position (dock->actor,
                              0.0f,
                              (gfloat) height - DOCK_HEIGHT);

  my_dock_layout_launchers (dock, (gfloat) width);
}

ClutterActor *
my_dock_get_actor (MyDock *dock)
{
  return dock ? dock->actor : NULL;
}

void
my_dock_populate_container (MetaContext   *context,
                            MetaDisplay   *display,
                            ClutterActor  *stage,
                            ClutterActor  *container)
{
  ClutterActor *spot_launcher;
  gsize i;

  spot_launcher = my_dock_create_spot_launcher (stage, display);
  clutter_actor_add_child (container, spot_launcher);
  clutter_actor_show (spot_launcher);

  for (i = 0; i < G_N_ELEMENTS (dock_apps); i++)
    {
      g_autoptr (GDesktopAppInfo) app_info = NULL;
      const char *executable;
      ClutterActor *launcher;

      app_info = my_dock_find_app_info (&dock_apps[i]);
      if (!app_info)
        continue;

      executable = g_app_info_get_executable (G_APP_INFO (app_info));
      if (!executable || !g_find_program_in_path (executable))
        continue;

      launcher = my_dock_create_launcher (context,
                                          display,
                                          stage,
                                          app_info,
                                          TRUE);
      clutter_actor_add_child (container, launcher);
      clutter_actor_show (launcher);
    }
}

void
my_dock_free (MyDock *dock)
{
  if (!dock)
    return;

  g_clear_pointer (&dock->actor, clutter_actor_destroy);
  g_clear_object (&dock->context);
  g_free (dock);
}
