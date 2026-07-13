#include "ooze-icon-lookup.h"
#include "ooze-shared-icons.h"

#include <string.h>

/*
 * Compositor-safe Freedesktop icon lookup (no GtkIconTheme).
 * Callers that load many icons (dock rebuild) must idle-defer off the
 * Clutter/GSettings click path — see ooze_dock_schedule_rebuild_icons and
 * OozeStall: dock-rebuild-icons.
 */

static const char *const icon_contexts[] = {
  "apps",
  "actions",
  "devices",
  "places",
  "mimes",
  "mimetypes",
  "categories",
  "status",
  "emblems",
  NULL,
};

static const int icon_sizes[] = { 256, 128, 96, 64, 48, 32, 24, 22, 16, 0 };

static char *
ooze_icon_get_gsettings_theme (void)
{
  g_autoptr (GSettings) settings = g_settings_new ("org.gnome.desktop.interface");

  return g_settings_get_string (settings, "icon-theme");
}

static void
ooze_icon_append_unique_path (GPtrArray  *paths,
                              const char *path)
{
  guint i;

  if (!path || path[0] == '\0')
    return;
  if (!g_file_test (path, G_FILE_TEST_IS_DIR))
    return;

  for (i = 0; i < paths->len; i++)
    {
      if (g_strcmp0 (paths->pdata[i], path) == 0)
        return;
    }

  g_ptr_array_add (paths, g_strdup (path));
}

static GPtrArray *
ooze_icon_collect_bases (void)
{
  GPtrArray *bases = g_ptr_array_new_with_free_func (g_free);
  g_autofree char *local_icons = ooze_icons_get_icons_dir ();
  g_autofree char *user_icons = NULL;
  const char * const *data_dirs;
  gsize i;

  ooze_icon_append_unique_path (bases, local_icons);

  user_icons = g_build_filename (g_get_user_data_dir (), "icons", NULL);
  ooze_icon_append_unique_path (bases, user_icons);

  data_dirs = g_get_system_data_dirs ();
  for (i = 0; data_dirs && data_dirs[i]; i++)
    {
      g_autofree char *dir = g_build_filename (data_dirs[i], "icons", NULL);
      ooze_icon_append_unique_path (bases, dir);
    }

  ooze_icon_append_unique_path (bases, "/usr/share/icons");
  ooze_icon_append_unique_path (bases, "/usr/share/pixmaps");

  return bases;
}

static GdkPixbuf *
ooze_icon_try_load_file (const char *path,
                         int         size)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;

  if (!path || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
    return NULL;

  pixbuf = gdk_pixbuf_new_from_file_at_scale (path, size, size, TRUE, &error);
  if (!pixbuf)
    return NULL;

  return g_steal_pointer (&pixbuf);
}

static GdkPixbuf *
ooze_icon_try_in_dir (const char *dir,
                      const char *icon_name,
                      int         size)
{
  static const char *extensions[] = { ".png", ".svg", ".xpm", NULL };
  gsize e;

  for (e = 0; extensions[e]; e++)
    {
      g_autofree char *path =
        g_strdup_printf ("%s/%s%s", dir, icon_name, extensions[e]);
      GdkPixbuf *pixbuf = ooze_icon_try_load_file (path, size);

      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static GdkPixbuf *
ooze_icon_scale_if_needed (GdkPixbuf *pixbuf,
                           int        size)
{
  int found_w;

  if (!pixbuf)
    return NULL;

  found_w = gdk_pixbuf_get_width (pixbuf);
  if (found_w > size)
    {
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                  size,
                                                  size,
                                                  GDK_INTERP_BILINEAR);
      g_object_unref (pixbuf);
      return scaled;
    }

  return pixbuf;
}

static GdkPixbuf *
ooze_icon_try_theme_in_base (const char *icon_base,
                             const char *theme,
                             const char *icon_name,
                             int         size)
{
  gsize c;
  gsize s;

  if (!theme || theme[0] == '\0' || !icon_name)
    return NULL;

  /* Prefer exact size under modern layout: theme/context/size/name */
  for (c = 0; icon_contexts[c]; c++)
    {
      g_autofree char *dir = g_strdup_printf ("%s/%s/%s/%d",
                                              icon_base, theme,
                                              icon_contexts[c], size);
      GdkPixbuf *pixbuf = ooze_icon_try_in_dir (dir, icon_name, size);

      if (pixbuf)
        return pixbuf;
    }

  for (s = 0; icon_sizes[s] != 0; s++)
    {
      if (icon_sizes[s] == size)
        continue;
      for (c = 0; icon_contexts[c]; c++)
        {
          g_autofree char *dir = g_strdup_printf ("%s/%s/%s/%d",
                                                  icon_base, theme,
                                                  icon_contexts[c],
                                                  icon_sizes[s]);
          GdkPixbuf *pixbuf = ooze_icon_try_in_dir (dir, icon_name,
                                                    icon_sizes[s]);

          if (pixbuf)
            return ooze_icon_scale_if_needed (pixbuf, size);
        }
    }

  /* Legacy: theme/NxN/context/name */
  for (c = 0; icon_contexts[c]; c++)
    {
      g_autofree char *dir = g_strdup_printf ("%s/%s/%dx%d/%s",
                                              icon_base, theme,
                                              size, size, icon_contexts[c]);
      GdkPixbuf *pixbuf = ooze_icon_try_in_dir (dir, icon_name, size);

      if (pixbuf)
        return pixbuf;
    }

  for (s = 0; icon_sizes[s] != 0; s++)
    {
      if (icon_sizes[s] == size)
        continue;
      for (c = 0; icon_contexts[c]; c++)
        {
          g_autofree char *dir =
            g_strdup_printf ("%s/%s/%dx%d/%s",
                             icon_base, theme,
                             icon_sizes[s], icon_sizes[s],
                             icon_contexts[c]);
          GdkPixbuf *pixbuf = ooze_icon_try_in_dir (dir, icon_name,
                                                    icon_sizes[s]);

          if (pixbuf)
            return ooze_icon_scale_if_needed (pixbuf, size);
        }
    }

  for (c = 0; icon_contexts[c]; c++)
    {
      g_autofree char *scalable =
        g_strdup_printf ("%s/%s/scalable/%s", icon_base, theme, icon_contexts[c]);
      GdkPixbuf *pixbuf = ooze_icon_try_in_dir (scalable, icon_name, size);

      if (pixbuf)
        return pixbuf;
    }

  /* Loose file under theme root or base (pixmaps). */
  {
    g_autofree char *theme_dir = g_build_filename (icon_base, theme, NULL);
    GdkPixbuf *pixbuf = ooze_icon_try_in_dir (theme_dir, icon_name, size);

    if (pixbuf)
      return pixbuf;
  }

  return ooze_icon_try_in_dir (icon_base, icon_name, size);
}

static void
ooze_icon_parse_inherits (const char *index_path,
                          GPtrArray  *chain)
{
  g_autoptr (GKeyFile) key = g_key_file_new ();
  g_autofree char *inherits = NULL;
  g_auto (GStrv) parts = NULL;
  gsize i;

  if (!g_key_file_load_from_file (key, index_path, G_KEY_FILE_NONE, NULL))
    return;

  inherits = g_key_file_get_string (key, "Icon Theme", "Inherits", NULL);
  if (!inherits)
    return;

  parts = g_strsplit (inherits, ",", -1);
  for (i = 0; parts && parts[i]; i++)
    {
      g_autofree char *name = g_strstrip (g_strdup (parts[i]));
      guint j;
      gboolean found = FALSE;

      if (!name || name[0] == '\0')
        continue;

      for (j = 0; j < chain->len; j++)
        {
          if (g_strcmp0 (chain->pdata[j], name) == 0)
            {
              found = TRUE;
              break;
            }
        }
      if (!found)
        g_ptr_array_add (chain, g_steal_pointer (&name));
    }
}

static GPtrArray *
ooze_icon_build_theme_chain (const char *primary,
                             GPtrArray  *bases)
{
  GPtrArray *chain = g_ptr_array_new_with_free_func (g_free);
  guint i;

  if (primary && primary[0] != '\0')
    g_ptr_array_add (chain, g_strdup (primary));

  /* Resolve Inherits= from primary theme's first matching index.theme */
  if (primary && primary[0] != '\0')
    {
      for (i = 0; i < bases->len; i++)
        {
          g_autofree char *index =
            g_build_filename (bases->pdata[i], primary, "index.theme", NULL);

          if (g_file_test (index, G_FILE_TEST_IS_REGULAR))
            {
              ooze_icon_parse_inherits (index, chain);
              break;
            }
        }
    }

  {
    static const char *fallbacks[] = {
      OOZE_ICON_THEME, "Adwaita", "Yaru", "hicolor", NULL
    };
    gsize f;

    for (f = 0; fallbacks[f]; f++)
      {
        guint j;
        gboolean found = FALSE;

        for (j = 0; j < chain->len; j++)
          {
            if (g_strcmp0 (chain->pdata[j], fallbacks[f]) == 0)
              {
                found = TRUE;
                break;
              }
          }
        if (!found)
          g_ptr_array_add (chain, g_strdup (fallbacks[f]));
      }
  }

  return chain;
}

GdkPixbuf *
ooze_icon_lookup_load (const char *icon_name,
                       int         size)
{
  g_autoptr (GPtrArray) bases = NULL;
  g_autoptr (GPtrArray) themes = NULL;
  g_autofree char *primary = NULL;
  guint b;
  guint t;

  if (!icon_name || icon_name[0] == '\0' || size <= 0)
    return NULL;

  if (icon_name[0] == '/' || g_path_is_absolute (icon_name))
    return ooze_icon_try_load_file (icon_name, size);

  /* Relative path with extension */
  if (strchr (icon_name, '/') != NULL)
    {
      GdkPixbuf *pixbuf = ooze_icon_try_load_file (icon_name, size);

      if (pixbuf)
        return pixbuf;
    }

  bases = ooze_icon_collect_bases ();
  primary = ooze_icon_get_gsettings_theme ();
  themes = ooze_icon_build_theme_chain (primary, bases);

  for (t = 0; t < themes->len; t++)
    {
      for (b = 0; b < bases->len; b++)
        {
          GdkPixbuf *pixbuf =
            ooze_icon_try_theme_in_base (bases->pdata[b],
                                         themes->pdata[t],
                                         icon_name,
                                         size);

          if (pixbuf)
            return pixbuf;
        }
    }

  return NULL;
}

GdkPixbuf *
ooze_icon_lookup_from_gicon (GIcon *icon,
                             int    size)
{
  if (!icon || size <= 0)
    return NULL;

  if (G_IS_THEMED_ICON (icon))
    {
      const char * const *names = g_themed_icon_get_names (G_THEMED_ICON (icon));

      return ooze_icon_lookup_first (names, size);
    }

  if (G_IS_FILE_ICON (icon))
    {
      GFile *file = g_file_icon_get_file (G_FILE_ICON (icon));
      g_autofree char *path = file ? g_file_get_path (file) : NULL;

      if (path)
        return ooze_icon_try_load_file (path, size);
    }

  if (G_IS_BYTES_ICON (icon))
    {
      GBytes *bytes = g_bytes_icon_get_bytes (G_BYTES_ICON (icon));
      gsize len = 0;
      gconstpointer data = bytes ? g_bytes_get_data (bytes, &len) : NULL;
      g_autoptr (GInputStream) stream = NULL;
      g_autoptr (GError) error = NULL;
      GdkPixbuf *pixbuf;

      if (!data || len == 0)
        return NULL;

      stream = g_memory_input_stream_new_from_data (data, (gssize) len, NULL);
      pixbuf = gdk_pixbuf_new_from_stream_at_scale (stream, size, size, TRUE,
                                                    NULL, &error);
      return pixbuf;
    }

  return NULL;
}

GdkPixbuf *
ooze_icon_lookup_first (const char * const *icon_names,
                        int                 size)
{
  gsize i;

  if (!icon_names)
    return NULL;

  for (i = 0; icon_names[i]; i++)
    {
      GdkPixbuf *pixbuf = ooze_icon_lookup_load (icon_names[i], size);

      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static gboolean
ooze_icon_theme_dir_is_icon_theme (const char *theme_dir)
{
  g_autofree char *index = g_build_filename (theme_dir, "index.theme", NULL);
  g_autoptr (GKeyFile) key = NULL;
  g_autofree char *dirs = NULL;

  if (!g_file_test (index, G_FILE_TEST_IS_REGULAR))
    return FALSE;

  key = g_key_file_new ();
  if (!g_key_file_load_from_file (key, index, G_KEY_FILE_NONE, NULL))
    return FALSE;

  /* Cursor-only themes often omit Directories= */
  dirs = g_key_file_get_string (key, "Icon Theme", "Directories", NULL);
  return dirs != NULL && dirs[0] != '\0';
}

static gboolean
ooze_icon_theme_dir_is_cursor_theme (const char *theme_dir)
{
  g_autofree char *cursors = g_build_filename (theme_dir, "cursors", NULL);
  g_autofree char *index = g_build_filename (theme_dir, "index.theme", NULL);

  if (g_file_test (cursors, G_FILE_TEST_IS_DIR))
    return TRUE;

  if (g_file_test (index, G_FILE_TEST_IS_REGULAR))
    {
      g_autoptr (GKeyFile) key = g_key_file_new ();
      g_autofree char *dirs = NULL;

      if (!g_key_file_load_from_file (key, index, G_KEY_FILE_NONE, NULL))
        return FALSE;
      dirs = g_key_file_get_string (key, "Icon Theme", "Directories", NULL);
      /* Many cursor themes have empty Directories */
      if (!dirs || dirs[0] == '\0')
        return TRUE;
    }

  return FALSE;
}

static void
ooze_icon_scan_theme_names (GPtrArray *names,
                            gboolean   cursors)
{
  g_autoptr (GPtrArray) bases = ooze_icon_collect_bases ();
  guint b;

  for (b = 0; b < bases->len; b++)
    {
      const char *base = bases->pdata[b];
      g_autoptr (GDir) dir = NULL;
      const char *name;

      /* pixmaps is not a theme container */
      if (g_str_has_suffix (base, "/pixmaps"))
        continue;

      dir = g_dir_open (base, 0, NULL);
      if (!dir)
        continue;

      while ((name = g_dir_read_name (dir)) != NULL)
        {
          g_autofree char *theme_dir = g_build_filename (base, name, NULL);
          guint i;
          gboolean found = FALSE;
          gboolean ok;

          if (!g_file_test (theme_dir, G_FILE_TEST_IS_DIR))
            continue;
          if (g_strcmp0 (name, "default") == 0)
            continue;

          ok = cursors ? ooze_icon_theme_dir_is_cursor_theme (theme_dir)
                       : ooze_icon_theme_dir_is_icon_theme (theme_dir);
          if (!ok)
            continue;

          for (i = 0; i < names->len; i++)
            {
              if (g_strcmp0 (names->pdata[i], name) == 0)
                {
                  found = TRUE;
                  break;
                }
            }
          if (!found)
            g_ptr_array_add (names, g_strdup (name));
        }
    }

  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
}

char **
ooze_icon_lookup_list_icon_themes (void)
{
  g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);

  ooze_icon_scan_theme_names (names, FALSE);
  g_ptr_array_add (names, NULL);
  return (char **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

char **
ooze_icon_lookup_list_cursor_themes (void)
{
  g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);

  ooze_icon_scan_theme_names (names, TRUE);
  g_ptr_array_add (names, NULL);
  return (char **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

gboolean
ooze_icon_lookup_theme_installed (const char *theme_name)
{
  g_autoptr (GPtrArray) bases = NULL;
  guint b;

  if (!theme_name || theme_name[0] == '\0')
    return FALSE;

  bases = ooze_icon_collect_bases ();
  for (b = 0; b < bases->len; b++)
    {
      g_autofree char *index =
        g_build_filename (bases->pdata[b], theme_name, "index.theme", NULL);

      if (g_file_test (index, G_FILE_TEST_IS_REGULAR))
        return TRUE;
    }

  return FALSE;
}
