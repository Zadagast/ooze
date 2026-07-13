#include "ooze-foreign-gtk.h"
#include "ooze-shared-icons.h"

#include <string.h>

char *
ooze_foreign_gtk_pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
                           "ooze",
                           "foreign-gtk-theme",
                           NULL);
}

static void
ooze_foreign_append_theme_base (GPtrArray  *bases,
                                const char *path)
{
  guint i;

  if (!path || path[0] == '\0')
    return;
  if (!g_file_test (path, G_FILE_TEST_IS_DIR))
    return;

  for (i = 0; i < bases->len; i++)
    {
      if (g_strcmp0 (bases->pdata[i], path) == 0)
        return;
    }

  g_ptr_array_add (bases, g_strdup (path));
}

static GPtrArray *
ooze_foreign_collect_theme_bases (void)
{
  GPtrArray *bases = g_ptr_array_new_with_free_func (g_free);
  g_autofree char *data_root = ooze_icons_get_data_root ();
  g_autofree char *user_themes = NULL;
  g_autofree char *home_themes = NULL;
  g_autofree char *ooze_themes = NULL;
  const char * const *data_dirs;
  gsize i;

  user_themes = g_build_filename (g_get_user_data_dir (), "themes", NULL);
  ooze_foreign_append_theme_base (bases, user_themes);

  home_themes = g_build_filename (g_get_home_dir (), ".themes", NULL);
  ooze_foreign_append_theme_base (bases, home_themes);

  if (data_root && data_root[0] != '\0')
    {
      g_autofree char *local = g_build_filename (data_root, "themes", NULL);

      ooze_foreign_append_theme_base (bases, local);
    }

  ooze_themes = g_strdup ("/usr/share/ooze/themes");
  ooze_foreign_append_theme_base (bases, ooze_themes);

  data_dirs = g_get_system_data_dirs ();
  for (i = 0; data_dirs && data_dirs[i]; i++)
    {
      g_autofree char *dir = g_build_filename (data_dirs[i], "themes", NULL);

      ooze_foreign_append_theme_base (bases, dir);
    }

  ooze_foreign_append_theme_base (bases, "/usr/share/themes");

  return bases;
}

gboolean
ooze_foreign_gtk_theme_installed (const char *theme_name)
{
  g_autoptr (GPtrArray) bases = NULL;
  guint b;

  if (!theme_name || theme_name[0] == '\0')
    return FALSE;
  if (g_strcmp0 (theme_name, OOZE_FOREIGN_GTK_AUTO) == 0)
    return TRUE;

  bases = ooze_foreign_collect_theme_bases ();
  for (b = 0; b < bases->len; b++)
    {
      g_autofree char *index =
        g_build_filename (bases->pdata[b], theme_name, "index.theme", NULL);

      if (g_file_test (index, G_FILE_TEST_IS_REGULAR))
        return TRUE;
    }

  return FALSE;
}

const char *
ooze_foreign_gtk_default_for_dark (gboolean dark)
{
  return dark ? OOZE_FOREIGN_GTK_DARK : OOZE_FOREIGN_GTK_LIGHT;
}

gboolean
ooze_foreign_gtk_variant_installed (gboolean dark)
{
  return ooze_foreign_gtk_theme_installed (ooze_foreign_gtk_default_for_dark (dark));
}

char *
ooze_foreign_gtk_pref_get (void)
{
  g_autofree char *path = ooze_foreign_gtk_pref_path ();
  g_autofree char *contents = NULL;
  gsize len = 0;
  char *value;

  if (!g_file_get_contents (path, &contents, &len, NULL) || !contents)
    return g_strdup (OOZE_FOREIGN_GTK_AUTO);

  value = g_strstrip (g_strdup (contents));
  if (!value || value[0] == '\0')
    {
      g_free (value);
      return g_strdup (OOZE_FOREIGN_GTK_AUTO);
    }

  return value;
}

gboolean
ooze_foreign_gtk_pref_set (const char *theme_or_auto)
{
  g_autofree char *path = ooze_foreign_gtk_pref_path ();
  g_autofree char *dir = NULL;
  const char *value = theme_or_auto && theme_or_auto[0]
                        ? theme_or_auto
                        : OOZE_FOREIGN_GTK_AUTO;

  dir = g_path_get_dirname (path);
  if (g_mkdir_with_parents (dir, 0700) != 0)
    return FALSE;

  return g_file_set_contents (path, value, -1, NULL);
}

static gboolean
ooze_foreign_session_is_dark (void)
{
  g_autoptr (GSettings) iface = g_settings_new ("org.gnome.desktop.interface");
  g_autofree char *scheme = NULL;

  if (!iface)
    return FALSE;

  scheme = g_settings_get_string (iface, "color-scheme");
  return g_strcmp0 (scheme, "prefer-dark") == 0;
}

char *
ooze_foreign_gtk_theme_for_session (void)
{
  g_autofree char *pref = ooze_foreign_gtk_pref_get ();
  gboolean dark = ooze_foreign_session_is_dark ();
  const char *candidate;

  if (pref && g_strcmp0 (pref, OOZE_FOREIGN_GTK_AUTO) != 0)
    {
      if (ooze_foreign_gtk_theme_installed (pref))
        return g_steal_pointer (&pref);
    }

  candidate = ooze_foreign_gtk_default_for_dark (dark);
  if (ooze_foreign_gtk_theme_installed (candidate))
    return g_strdup (candidate);

  candidate = ooze_foreign_gtk_default_for_dark (!dark);
  if (ooze_foreign_gtk_theme_installed (candidate))
    return g_strdup (candidate);

  return NULL;
}

char **
ooze_foreign_gtk_list_themes (void)
{
  g_autoptr (GPtrArray) bases = ooze_foreign_collect_theme_bases ();
  g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
  guint b;

  for (b = 0; b < bases->len; b++)
    {
      g_autoptr (GDir) dir = g_dir_open (bases->pdata[b], 0, NULL);
      const char *name;

      if (!dir)
        continue;

      while ((name = g_dir_read_name (dir)) != NULL)
        {
          g_autofree char *index =
            g_build_filename (bases->pdata[b], name, "index.theme", NULL);
          guint i;
          gboolean found = FALSE;

          if (!g_file_test (index, G_FILE_TEST_IS_REGULAR))
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
  g_ptr_array_add (names, NULL);
  return (char **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

void
ooze_foreign_gtk_apply_to_launcher (GSubprocessLauncher *launcher)
{
  g_autofree char *name = NULL;

  if (!launcher)
    return;

  name = ooze_foreign_gtk_theme_for_session ();
  if (name)
    g_subprocess_launcher_setenv (launcher, "GTK_THEME", name, TRUE);
}

void
ooze_foreign_gtk_apply_to_launch_context (GAppLaunchContext *ctx)
{
  g_autofree char *name = NULL;

  if (!ctx)
    return;

  name = ooze_foreign_gtk_theme_for_session ();
  if (name)
    g_app_launch_context_setenv (ctx, "GTK_THEME", name);
}
