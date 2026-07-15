#include "ooze-autostart.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

/*
 * gnome-session runs XDG autostart entries for a GNOME session; nothing
 * does it for Ooze, so network applets, cloud-sync agents and the like
 * never start. This is the Ooze session's autostart pass: user entries
 * shadow system ones by basename, and standard keys decide eligibility.
 */

/* Let the shell finish coming up before spawning session helpers. */
#define OOZE_AUTOSTART_DELAY_MS 2000

static gboolean
ooze_autostart_entry_enabled (GKeyFile *keyfile)
{
  g_autofree char *try_exec = NULL;

  if (g_key_file_get_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                              G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL))
    return FALSE;

  /* Legacy GNOME toggle still written by some apps. */
  if (g_key_file_has_key (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                          "X-GNOME-Autostart-enabled", NULL) &&
      !g_key_file_get_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                               "X-GNOME-Autostart-enabled", NULL))
    return FALSE;

  try_exec = g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
                                    G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL);
  if (try_exec && *try_exec)
    {
      g_autofree char *program = g_find_program_in_path (try_exec);

      if (!program)
        return FALSE;
    }

  return TRUE;
}

static void
ooze_autostart_launch_file (const char *path)
{
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autoptr (GDesktopAppInfo) info = NULL;
  g_autoptr (GError) error = NULL;

  if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
    return;

  if (!ooze_autostart_entry_enabled (keyfile))
    return;

  info = g_desktop_app_info_new_from_keyfile (keyfile);
  if (!info)
    return;

  /* OnlyShowIn / NotShowIn against XDG_CURRENT_DESKTOP (Ooze). */
  if (!g_desktop_app_info_get_show_in (info, NULL))
    return;

  if (!g_app_info_launch (G_APP_INFO (info), NULL, NULL, &error))
    g_warning ("Ooze autostart: failed to launch %s: %s",
               path, error->message);
  else
    g_print ("Ooze autostart: launched %s\n", path);
}

static void
ooze_autostart_collect_dir (const char *dir_path,
                            GHashTable *entries)
{
  g_autoptr (GDir) dir = NULL;
  const char *name;

  dir = g_dir_open (dir_path, 0, NULL);
  if (!dir)
    return;

  while ((name = g_dir_read_name (dir)))
    {
      if (!g_str_has_suffix (name, ".desktop"))
        continue;
      if (g_hash_table_contains (entries, name))
        continue;

      g_hash_table_insert (entries,
                           g_strdup (name),
                           g_build_filename (dir_path, name, NULL));
    }
}

static gboolean
ooze_autostart_timeout (gpointer user_data G_GNUC_UNUSED)
{
  g_autoptr (GHashTable) entries = NULL;
  const char *const *system_dirs;
  g_autofree char *user_dir = NULL;
  GHashTableIter iter;
  gpointer path;
  gsize i;

  entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, g_free);

  /* User entries shadow system entries with the same basename. */
  user_dir = g_build_filename (g_get_user_config_dir (), "autostart", NULL);
  ooze_autostart_collect_dir (user_dir, entries);

  system_dirs = g_get_system_config_dirs ();
  for (i = 0; system_dirs && system_dirs[i]; i++)
    {
      g_autofree char *dir_path =
        g_build_filename (system_dirs[i], "autostart", NULL);

      ooze_autostart_collect_dir (dir_path, entries);
    }

  g_hash_table_iter_init (&iter, entries);
  while (g_hash_table_iter_next (&iter, NULL, &path))
    ooze_autostart_launch_file (path);

  return G_SOURCE_REMOVE;
}

void
ooze_autostart_run (void)
{
  static gboolean started;

  /* Nested devkit shares the host session: agents are already running. */
  if (g_strcmp0 (g_getenv ("OOZE_DEVKIT"), "1") == 0)
    return;

  if (started)
    return;
  started = TRUE;

  g_timeout_add_full (G_PRIORITY_LOW, OOZE_AUTOSTART_DELAY_MS,
                      ooze_autostart_timeout, NULL, NULL);
}
