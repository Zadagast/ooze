#ifdef __linux__
#define _DEFAULT_SOURCE 1
#endif

#include "ooze-shared-icons.h"

#include <gio/gio.h>

#ifdef __linux__
#include <unistd.h>
#endif

static gboolean
ooze_icons_has_elementary_theme (const char *data_root)
{
  g_autofree char *index = NULL;

  if (!data_root || data_root[0] == '\0')
    return FALSE;

  index = g_build_filename (data_root,
                            "icons",
                            OOZE_ICON_THEME,
                            "index.theme",
                            NULL);
  return g_file_test (index, G_FILE_TEST_IS_REGULAR);
}

static char *
ooze_icons_get_data_root_from_exe (void)
{
#ifdef __linux__
  char exe_path[4096];
  ssize_t len;

  len = readlink ("/proc/self/exe", exe_path, sizeof (exe_path) - 1);
  if (len > 0)
    {
      g_autofree char *dir = NULL;
      g_autofree char *data = NULL;
      char *canonical;

      exe_path[len] = '\0';
      dir = g_path_get_dirname (exe_path);
      data = g_build_filename (dir, "..", "data", NULL);
      if (ooze_icons_has_elementary_theme (data))
        {
          canonical = g_canonicalize_filename (data, NULL);
          if (canonical)
            return canonical;
        }
    }
#endif

  return NULL;
}

char *
ooze_icons_get_data_root (void)
{
  const char *env_dir;

  env_dir = g_getenv ("OOZE_DATA_DIR");
  if (env_dir && env_dir[0] != '\0')
    return g_strdup (env_dir);

  {
    g_autofree char *from_exe = ooze_icons_get_data_root_from_exe ();

    if (from_exe)
      return g_steal_pointer (&from_exe);
  }

#ifdef OOZE_DATA_DIR
  if (ooze_icons_has_elementary_theme (OOZE_DATA_DIR))
    return g_strdup (OOZE_DATA_DIR);
#endif

  {
    g_autofree char *cwd_data = g_build_filename (g_get_current_dir (), "data", NULL);

    if (ooze_icons_has_elementary_theme (cwd_data))
      return g_steal_pointer (&cwd_data);
  }

  return g_build_filename (g_get_current_dir (), "data", NULL);
}

char *
ooze_icons_get_icons_dir (void)
{
  g_autofree char *data_root = ooze_icons_get_data_root ();

  return g_build_filename (data_root, "icons", NULL);
}

char *
ooze_icons_get_elementary_dir (void)
{
  g_autofree char *icons_dir = ooze_icons_get_icons_dir ();

  return g_build_filename (icons_dir, OOZE_ICON_THEME, NULL);
}

static gboolean
ooze_icons_path_in_search_list (const char *list,
                              const char *path)
{
  const char *cursor;
  gsize path_len;

  if (!path || path[0] == '\0')
    return FALSE;

  if (!list || list[0] == '\0')
    return FALSE;

  path_len = strlen (path);
  for (cursor = list; *cursor != '\0'; )
    {
      const char *end = strchr (cursor, G_SEARCHPATH_SEPARATOR);
      gsize segment_len = end ? (gsize) (end - cursor) : strlen (cursor);

      if (segment_len == path_len &&
          strncmp (cursor, path, path_len) == 0)
        return TRUE;

      cursor = end ? end + 1 : cursor + segment_len;
    }

  return FALSE;
}

void
ooze_icons_setup_environment (void)
{
  g_autofree char *data_root = ooze_icons_get_data_root ();
  const char *existing;

  if (!data_root || data_root[0] == '\0')
    return;

  existing = g_getenv ("XDG_DATA_DIRS");
  if (!ooze_icons_path_in_search_list (existing, data_root))
    {
      g_autofree char *updated = NULL;

      if (existing && existing[0] != '\0')
        {
          updated = g_strdup_printf ("%s%c%s",
                                     data_root,
                                     G_SEARCHPATH_SEPARATOR,
                                     existing);
        }
      else
        {
          updated = g_strdup (data_root);
        }

      g_setenv ("XDG_DATA_DIRS", updated, TRUE);
    }
}

static gboolean
ooze_icons_has_index_theme (const char *dir)
{
  g_autofree char *index = NULL;

  if (!dir || dir[0] == '\0')
    return FALSE;

  index = g_build_filename (dir, "index.theme", NULL);
  return g_file_test (index, G_FILE_TEST_IS_REGULAR);
}

gboolean
ooze_icons_theme_is_available (void)
{
  g_autofree char *local = ooze_icons_get_elementary_dir ();

  if (ooze_icons_has_index_theme (local))
    return TRUE;

  return ooze_icons_has_index_theme ("/usr/share/icons/" OOZE_ICON_THEME);
}

static gboolean
ooze_icons_theme_name_usable (const char *theme_name)
{
  g_autofree char *local = NULL;
  g_autofree char *system = NULL;

  if (!theme_name || theme_name[0] == '\0')
    return FALSE;

  local = g_build_filename (g_get_user_data_dir (),
                            "icons",
                            theme_name,
                            "index.theme",
                            NULL);
  if (g_file_test (local, G_FILE_TEST_IS_REGULAR))
    return TRUE;

  system = g_build_filename ("/usr/share/icons",
                             theme_name,
                             "index.theme",
                             NULL);
  if (g_file_test (system, G_FILE_TEST_IS_REGULAR))
    return TRUE;

  {
    g_autofree char *ooze = NULL;
    g_autofree char *data_root = ooze_icons_get_data_root ();

    if (data_root && data_root[0] != '\0')
      {
        ooze = g_build_filename (data_root,
                                 "icons",
                                 theme_name,
                                 "index.theme",
                                 NULL);
        if (g_file_test (ooze, G_FILE_TEST_IS_REGULAR))
          return TRUE;
      }
  }

  return g_file_test ("/usr/share/ooze/icons/" OOZE_ICON_THEME "/index.theme",
                      G_FILE_TEST_IS_REGULAR) &&
         g_strcmp0 (theme_name, OOZE_ICON_THEME) == 0;
}

void
ooze_icons_apply (void)
{
  g_autoptr (GSettings) settings = NULL;
  g_autofree char *icon_theme = NULL;
  g_autofree char *cursor_theme = NULL;
  int cursor_size;

  ooze_icons_setup_environment ();

  settings = g_settings_new ("org.gnome.desktop.interface");
  icon_theme = g_settings_get_string (settings, "icon-theme");
  cursor_theme = g_settings_get_string (settings, "cursor-theme");
  cursor_size = g_settings_get_int (settings, "cursor-size");

  /* Only seed defaults — never clobber a user-chosen Mint/Papirus/etc pack. */
  if (!ooze_icons_theme_name_usable (icon_theme))
    g_settings_set_string (settings, "icon-theme", OOZE_ICON_THEME);

  if (!cursor_theme || cursor_theme[0] == '\0')
    g_settings_set_string (settings, "cursor-theme", OOZE_CURSOR_THEME);

  if (cursor_size <= 0)
    g_settings_set_int (settings, "cursor-size", OOZE_CURSOR_SIZE);

  if (!ooze_icons_theme_is_available ())
    g_warning ("Ooze: elementary icon theme not found; run ninja -C build to fetch icons");
}

void
ooze_icons_apply_to_launcher (GSubprocessLauncher *launcher)
{
  g_autofree char *data_root = ooze_icons_get_data_root ();
  const char *gsettings_backend;
  const char *xdg_data_dirs;

  g_return_if_fail (launcher != NULL);

  ooze_icons_setup_environment ();

  if (data_root && data_root[0] != '\0')
    g_subprocess_launcher_setenv (launcher, "OOZE_DATA_DIR", data_root, TRUE);

  xdg_data_dirs = g_getenv ("XDG_DATA_DIRS");
  if (xdg_data_dirs && xdg_data_dirs[0] != '\0')
    g_subprocess_launcher_setenv (launcher, "XDG_DATA_DIRS", xdg_data_dirs, TRUE);

  /* Forward the GSettings backend to children – but never the "memory"
   * backend, because each process would get its own isolated store and
   * theme changes written by the compositor would be invisible to apps. */
  gsettings_backend = g_getenv ("GSETTINGS_BACKEND");
  if (gsettings_backend && gsettings_backend[0] != '\0'
      && g_strcmp0 (gsettings_backend, "memory") != 0)
    g_subprocess_launcher_setenv (launcher, "GSETTINGS_BACKEND", gsettings_backend, TRUE);

  {
    const char *schema_dir = g_getenv ("GSETTINGS_SCHEMA_DIR");

    if (!schema_dir || schema_dir[0] == '\0')
      schema_dir = "/usr/share/glib-2.0/schemas";
    g_subprocess_launcher_setenv (launcher, "GSETTINGS_SCHEMA_DIR", schema_dir, TRUE);
  }
}
