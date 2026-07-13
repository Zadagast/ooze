#include "ooze-dnd-bridge.h"

#include <glib/gstdio.h>
#include <string.h>

static char *
ooze_dnd_bridge_path (void)
{
  return g_build_filename (g_get_user_runtime_dir (), "ooze-dnd-pending", NULL);
}

static char *
ooze_dnd_bridge_hover_path (void)
{
  return g_build_filename (g_get_user_runtime_dir (), "ooze-dnd-hover", NULL);
}

void
ooze_dnd_bridge_clear (void)
{
  g_autofree char *path = ooze_dnd_bridge_path ();
  g_autofree char *hover = ooze_dnd_bridge_hover_path ();
  g_unlink (path);
  g_unlink (hover);
}

void
ooze_dnd_bridge_set_hover_dir (const char *dir_or_null)
{
  g_autofree char *path = ooze_dnd_bridge_hover_path ();

  if (!dir_or_null || !*dir_or_null)
    {
      g_unlink (path);
      return;
    }
  g_file_set_contents (path, dir_or_null, -1, NULL);
}

char *
ooze_dnd_bridge_get_hover_dir (void)
{
  g_autofree char *path = ooze_dnd_bridge_hover_path ();
  g_autofree char *contents = NULL;

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    return NULL;
  g_strstrip (contents);
  if (!*contents)
    return NULL;
  return g_steal_pointer (&contents);
}

void
ooze_dnd_bridge_set_paths (const char * const *paths,
                           gsize               n_paths,
                           gboolean            prefer_move)
{
  g_autofree char *path = ooze_dnd_bridge_path ();
  g_autoptr (GString) body = g_string_new (NULL);
  gsize i;

  g_string_append_c (body, prefer_move ? 'M' : 'C');
  g_string_append_c (body, '\n');
  for (i = 0; i < n_paths; i++)
    {
      if (!paths[i] || !*paths[i])
        continue;
      g_string_append (body, paths[i]);
      g_string_append_c (body, '\n');
    }

  g_file_set_contents (path, body->str, body->len, NULL);
}

void
ooze_dnd_bridge_set_files (GFile **files,
                           gsize   n_files,
                           gboolean prefer_move)
{
  g_autoptr (GPtrArray) paths = g_ptr_array_new_with_free_func (g_free);
  gsize i;

  for (i = 0; i < n_files; i++)
    {
      char *p;

      if (!files[i])
        continue;
      p = g_file_get_path (files[i]);
      if (p)
        g_ptr_array_add (paths, p);
    }

  ooze_dnd_bridge_set_paths ((const char * const *) paths->pdata,
                             paths->len,
                             prefer_move);
}

gboolean
ooze_dnd_bridge_take (char    ***paths_out,
                      gboolean  *prefer_move_out)
{
  g_autofree char *path = ooze_dnd_bridge_path ();
  g_autofree char *contents = NULL;
  gsize len = 0;
  g_auto (GStrv) lines = NULL;
  g_autoptr (GPtrArray) paths = NULL;
  gboolean prefer_move = FALSE;
  guint i;

  if (!paths_out)
    return FALSE;

  *paths_out = NULL;
  if (prefer_move_out)
    *prefer_move_out = FALSE;

  if (!g_file_get_contents (path, &contents, &len, NULL) || len < 2)
    return FALSE;

  lines = g_strsplit (contents, "\n", -1);
  if (!lines || !lines[0])
    {
      ooze_dnd_bridge_clear ();
      return FALSE;
    }

  prefer_move = (lines[0][0] == 'M' || lines[0][0] == 'm');
  paths = g_ptr_array_new_with_free_func (g_free);
  for (i = 1; lines[i] != NULL; i++)
    {
      if (!*lines[i])
        continue;
      g_ptr_array_add (paths, g_strdup (lines[i]));
    }

  ooze_dnd_bridge_clear ();

  if (paths->len == 0)
    return FALSE;

  g_ptr_array_add (paths, NULL);
  *paths_out = (char **) g_ptr_array_free (paths, FALSE);
  paths = NULL;
  if (prefer_move_out)
    *prefer_move_out = prefer_move;
  return TRUE;
}

gboolean
ooze_dnd_bridge_has_pending (void)
{
  g_autofree char *path = ooze_dnd_bridge_path ();
  return g_file_test (path, G_FILE_TEST_IS_REGULAR);
}

static gboolean
ooze_dnd_same_fs (GFile *a, GFile *b)
{
  g_autoptr (GFileInfo) ia = NULL;
  g_autoptr (GFileInfo) ib = NULL;
  const char *ida, *idb;

  ia = g_file_query_info (a, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  ib = g_file_query_info (b, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (!ia || !ib)
    return FALSE;
  ida = g_file_info_get_attribute_string (ia, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  idb = g_file_info_get_attribute_string (ib, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  return ida && idb && g_strcmp0 (ida, idb) == 0;
}

static gboolean
ooze_dnd_is_descendant (GFile *path, GFile *ancestor)
{
  g_autofree char *p = NULL;
  g_autofree char *a = NULL;

  if (!path || !ancestor)
    return FALSE;
  p = g_file_get_path (path);
  a = g_file_get_path (ancestor);
  if (!p || !a)
    return FALSE;
  if (g_strcmp0 (p, a) == 0)
    return TRUE;
  {
    g_autofree char *prefix = g_strconcat (a, G_DIR_SEPARATOR_S, NULL);
    return g_str_has_prefix (p, prefix);
  }
}

static char *
ooze_dnd_unique_name (GFile *parent, const char *basename)
{
  g_autoptr (GFile) child = NULL;
  g_autofree char *name = NULL;
  int n = 1;

  child = g_file_get_child (parent, basename);
  if (!g_file_query_exists (child, NULL))
    return g_strdup (basename);

  while (TRUE)
    {
      g_clear_object (&child);
      name = g_strdup_printf ("%s (%d)", basename, n++);
      child = g_file_get_child (parent, name);
      if (!g_file_query_exists (child, NULL))
        return g_steal_pointer (&name);
      g_clear_pointer (&name, g_free);
    }
}

static gboolean
ooze_dnd_copy_recursive (GFile *src, GFile *dest, GError **error)
{
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  info = g_file_query_info (src, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, error);
  if (!info)
    return FALSE;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    {
      g_autoptr (GFileEnumerator) en = NULL;
      GFileInfo *child_info;

      if (!g_file_make_directory_with_parents (dest, NULL, error))
        {
          if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            return FALSE;
          g_clear_error (error);
        }

      en = g_file_enumerate_children (src, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                      G_FILE_QUERY_INFO_NONE, NULL, error);
      if (!en)
        return FALSE;

      while ((child_info = g_file_enumerator_next_file (en, NULL, error)) != NULL)
        {
          const char *name = g_file_info_get_name (child_info);
          g_autoptr (GFile) child_src = g_file_get_child (src, name);
          g_autoptr (GFile) child_dest = g_file_get_child (dest, name);
          gboolean ok = ooze_dnd_copy_recursive (child_src, child_dest, error);
          g_object_unref (child_info);
          if (!ok)
            return FALSE;
        }
      return *error == NULL;
    }

  return g_file_copy (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
}

gboolean
ooze_dnd_bridge_drop_into (const char *dest_dir)
{
  g_auto (GStrv) paths = NULL;
  gboolean prefer_move = FALSE;
  g_autoptr (GFile) dest = NULL;
  guint i;
  gboolean any = FALSE;

  if (!dest_dir || !*dest_dir)
    return FALSE;
  if (!ooze_dnd_bridge_take (&paths, &prefer_move) || !paths)
    return FALSE;

  dest = g_file_new_for_path (dest_dir);
  g_mkdir_with_parents (dest_dir, 0700);

  for (i = 0; paths[i] != NULL; i++)
    {
      g_autoptr (GFile) src = g_file_new_for_path (paths[i]);
      g_autofree char *basename = NULL;
      g_autofree char *dest_name = NULL;
      g_autoptr (GFile) target = NULL;
      g_autoptr (GFile) src_parent = NULL;
      g_autoptr (GError) error = NULL;
      gboolean move;

      if (!g_file_query_exists (src, NULL))
        continue;
      if (ooze_dnd_is_descendant (dest, src))
        continue;

      src_parent = g_file_get_parent (src);
      if (src_parent && g_file_equal (src_parent, dest))
        continue;

      basename = g_file_get_basename (src);
      dest_name = ooze_dnd_unique_name (dest, basename);
      target = g_file_get_child (dest, dest_name);

      move = prefer_move || ooze_dnd_same_fs (src, dest);
      if (move)
        {
          if (!g_file_move (src, target, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
            {
              g_clear_error (&error);
              if (!ooze_dnd_copy_recursive (src, target, &error))
                g_warning ("Ooze DnD: %s", error->message);
            }
        }
      else if (!ooze_dnd_copy_recursive (src, target, &error))
        {
          g_warning ("Ooze DnD: %s", error->message);
        }
      any = TRUE;
    }

  return any;
}
