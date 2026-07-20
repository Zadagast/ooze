#include "ooze-dock-pins.h"

#include <gio/gio.h>
#include <string.h>

/* Ooze Launch (leftmost) and Spot (second) are added as fixed items by the
 * dock itself, so they are not listed here. Downloads + Trash are fixed at the
 * end. This is the curated default lineup between them. */
static const char * const ooze_dock_default_pins[] = {
  "firefox",
  "org.ooze.Command",
  "org.ooze.Eye",
  "org.ooze.Ear",
  "org.ooze.Scenery",
  "org.ooze.King",
  "org.ooze.Pak",
  NULL,
};

static gboolean
ooze_dock_pins_contains (GPtrArray   *pins,
                         const char  *needle)
{
  gsize i;

  for (i = 0; pins && i < pins->len; i++)
    {
      if (g_strcmp0 (pins->pdata[i], needle) == 0)
        return TRUE;
    }

  return FALSE;
}

const char * const *
ooze_dock_pins_defaults (void)
{
  return ooze_dock_default_pins;
}

static char *
ooze_dock_pins_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "ooze", "dock-pins", NULL);
}

char **
ooze_dock_pins_load (void)
{
  g_autofree char *path = ooze_dock_pins_path ();
  g_autofree char *contents = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) lines = NULL;
  GPtrArray *out;
  gsize i;

  if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    return g_strdupv ((char **) ooze_dock_default_pins);

  if (!g_file_get_contents (path, &contents, NULL, &error))
    {
      g_warning ("OozeDock: could not read %s: %s", path, error->message);
      return g_strdupv ((char **) ooze_dock_default_pins);
    }

  lines = g_strsplit (contents, "\n", -1);
  out = g_ptr_array_new_with_free_func (g_free);
  for (i = 0; lines && lines[i]; i++)
    {
      g_strstrip (lines[i]);
      if (lines[i][0] == '\0' || lines[i][0] == '#')
        continue;
      /* Trash, Downloads, Launch and Spot are fixed items added by the dock. */
      if (g_strcmp0 (lines[i], "org.ooze.Trash") == 0 ||
          g_strcmp0 (lines[i], "org.ooze.Downloads") == 0 ||
          g_strcmp0 (lines[i], "org.ooze.Launch") == 0 ||
          g_strcmp0 (lines[i], "org.ooze.Spot") == 0)
        continue;
      g_ptr_array_add (out, g_strdup (lines[i]));
    }

  if (!ooze_dock_pins_contains (out, "org.ooze.Scenery"))
    g_ptr_array_add (out, g_strdup ("org.ooze.Scenery"));
  g_ptr_array_add (out, NULL);
  return (char **) g_ptr_array_free (out, FALSE);
}

void
ooze_dock_pins_save (char **app_ids)
{
  g_autofree char *path = ooze_dock_pins_path ();
  g_autofree char *dir = g_path_get_dirname (path);
  g_autoptr (GString) body = g_string_new ("# Ooze dock pinned apps (one app-id per line)\n");
  g_autoptr (GError) error = NULL;
  gsize i;

  g_mkdir_with_parents (dir, 0700);

  for (i = 0; app_ids && app_ids[i]; i++)
    {
      if (!app_ids[i][0])
        continue;
      g_string_append (body, app_ids[i]);
      g_string_append_c (body, '\n');
    }

  if (!g_file_set_contents (path, body->str, (gssize) body->len, &error))
    g_warning ("OozeDock: could not write %s: %s", path, error->message);
}
