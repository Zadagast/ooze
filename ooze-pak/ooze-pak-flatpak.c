#include "ooze-pak-flatpak.h"

#include <gio/gio.h>
#include <string.h>

void
ooze_pak_app_free (OozePakApp *app)
{
  if (!app)
    return;
  g_free (app->app_id);
  g_free (app->name);
  g_free (app->version);
  g_free (app->branch);
  g_free (app->origin);
  g_free (app->installation);
  g_free (app);
}

gboolean
ooze_pak_flatpak_available (void)
{
  g_autofree char *bin = g_find_program_in_path ("flatpak");
  return bin != NULL;
}

gboolean
ooze_pak_path_looks_installable (const char *path)
{
  g_autofree char *lower = NULL;

  if (!path || !*path)
    return FALSE;

  lower = g_ascii_strdown (path, -1);
  return g_str_has_suffix (lower, ".flatpak")
      || g_str_has_suffix (lower, ".flatpakref")
      || g_str_has_suffix (lower, ".flatpakrepo");
}

static OozePakApp *
parse_list_line (const char *line)
{
  g_auto (GStrv) cols = NULL;
  OozePakApp *app;
  guint n;

  if (!line || !*line || line[0] == '#')
    return NULL;

  cols = g_strsplit (line, "\t", -1);
  n = g_strv_length (cols);
  if (n < 1 || !cols[0] || !*cols[0])
    return NULL;

  app = g_new0 (OozePakApp, 1);
  app->app_id = g_strdup (g_strstrip (cols[0]));
  app->name = g_strdup (n > 1 && cols[1] && *cols[1] ? g_strstrip (cols[1]) : app->app_id);
  app->version = g_strdup (n > 2 && cols[2] ? g_strstrip (cols[2]) : "");
  app->branch = g_strdup (n > 3 && cols[3] ? g_strstrip (cols[3]) : "stable");
  app->origin = g_strdup (n > 4 && cols[4] ? g_strstrip (cols[4]) : "");
  app->installation = g_strdup (n > 5 && cols[5] ? g_strstrip (cols[5]) : "user");
  return app;
}

static int
app_cmp_name (gconstpointer a, gconstpointer b)
{
  const OozePakApp *aa = a;
  const OozePakApp *bb = b;
  return g_ascii_strcasecmp (aa->name ? aa->name : "",
                             bb->name ? bb->name : "");
}

GList *
ooze_pak_flatpak_list_apps (GError **error)
{
  g_autoptr (GSubprocess) proc = NULL;
  g_autofree char *stdout_buf = NULL;
  g_autofree char *stderr_buf = NULL;
  g_auto (GStrv) lines = NULL;
  GList *apps = NULL;
  guint i;

  if (!ooze_pak_flatpak_available ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "flatpak is not installed");
      return NULL;
    }

  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           error,
                           "flatpak", "list", "--app",
                           "--columns=application,name,version,branch,origin,installation",
                           NULL);
  if (!proc)
    return NULL;

  if (!g_subprocess_communicate_utf8 (proc, NULL, NULL,
                                      &stdout_buf, &stderr_buf, error))
    return NULL;

  if (!g_subprocess_get_if_exited (proc) || g_subprocess_get_exit_status (proc) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "%s",
                   (stderr_buf && *stderr_buf) ? stderr_buf : "flatpak list failed");
      return NULL;
    }

  lines = g_strsplit (stdout_buf ? stdout_buf : "", "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      OozePakApp *app = parse_list_line (lines[i]);
      if (app)
        apps = g_list_prepend (apps, app);
    }

  return g_list_sort (apps, app_cmp_name);
}

typedef struct {
  OozePakFlatpakDone callback;
  gpointer user_data;
  char *action;
} AsyncCtx;

static void
async_ctx_free (AsyncCtx *ctx)
{
  g_free (ctx->action);
  g_free (ctx);
}

static void
on_proc_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  AsyncCtx *ctx = user_data;
  g_autoptr (GError) error = NULL;
  g_autofree char *stdout_buf = NULL;
  g_autofree char *stderr_buf = NULL;
  gboolean ok;

  ok = g_subprocess_communicate_utf8_finish (G_SUBPROCESS (source),
                                             result,
                                             &stdout_buf,
                                             &stderr_buf,
                                             &error);
  if (!ok)
    {
      if (ctx->callback)
        ctx->callback (FALSE, error->message, ctx->user_data);
      async_ctx_free (ctx);
      return;
    }

  if (!g_subprocess_get_if_exited (G_SUBPROCESS (source))
      || g_subprocess_get_exit_status (G_SUBPROCESS (source)) != 0)
    {
      const char *msg = (stderr_buf && *stderr_buf) ? stderr_buf
                        : (stdout_buf && *stdout_buf) ? stdout_buf
                        : "flatpak command failed";
      if (ctx->callback)
        ctx->callback (FALSE, msg, ctx->user_data);
      async_ctx_free (ctx);
      return;
    }

  if (ctx->callback)
    ctx->callback (TRUE, ctx->action, ctx->user_data);
  async_ctx_free (ctx);
}

void
ooze_pak_flatpak_install_async (const char         *path_or_ref,
                                OozePakFlatpakDone  callback,
                                gpointer            user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GSubprocess) proc = NULL;
  AsyncCtx *ctx;

  if (!path_or_ref || !*path_or_ref)
    {
      if (callback)
        callback (FALSE, "Nothing to install", user_data);
      return;
    }

  if (!ooze_pak_flatpak_available ())
    {
      if (callback)
        callback (FALSE, "flatpak is not installed", user_data);
      return;
    }

  /* Prefer user install for local bundles; -y for noninteractive. */
  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           &error,
                           "flatpak", "install", "--user", "-y", "--noninteractive",
                           path_or_ref,
                           NULL);
  if (!proc)
    {
      if (callback)
        callback (FALSE, error->message, user_data);
      return;
    }

  ctx = g_new0 (AsyncCtx, 1);
  ctx->callback = callback;
  ctx->user_data = user_data;
  ctx->action = g_strdup_printf ("Installed %s", path_or_ref);

  g_subprocess_communicate_utf8_async (proc, NULL, NULL, on_proc_done, ctx);
}

void
ooze_pak_flatpak_uninstall_async (const char         *app_id,
                                  const char         *installation,
                                  OozePakFlatpakDone  callback,
                                  gpointer            user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GSubprocess) proc = NULL;
  AsyncCtx *ctx;
  const char *scope;

  if (!app_id || !*app_id)
    {
      if (callback)
        callback (FALSE, "No application id", user_data);
      return;
    }

  if (!ooze_pak_flatpak_available ())
    {
      if (callback)
        callback (FALSE, "flatpak is not installed", user_data);
      return;
    }

  scope = (installation && g_strcmp0 (installation, "system") == 0)
          ? "--system" : "--user";

  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           &error,
                           "flatpak", "uninstall", scope, "-y", "--noninteractive",
                           app_id,
                           NULL);
  if (!proc)
    {
      if (callback)
        callback (FALSE, error->message, user_data);
      return;
    }

  ctx = g_new0 (AsyncCtx, 1);
  ctx->callback = callback;
  ctx->user_data = user_data;
  ctx->action = g_strdup_printf ("Removed %s", app_id);

  g_subprocess_communicate_utf8_async (proc, NULL, NULL, on_proc_done, ctx);
}
