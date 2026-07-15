#include "portal-backend.h"

typedef struct
{
  GMainLoop *loop;
  OozePortalBackend *backend;
  GDBusConnection *connection;
} PortalRuntime;

static void on_connection_closed (GDBusConnection *connection,
                                   gboolean         remote_peer_vanished,
                                   GError          *error,
                                   gpointer         user_data);

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  PortalRuntime *runtime = user_data;

  if (runtime->backend)
    ooze_portal_backend_stop (g_steal_pointer (&runtime->backend));
  g_set_object (&runtime->connection, connection);
  runtime->backend = ooze_portal_backend_start (connection);
  g_signal_connect (connection, "closed",
                    G_CALLBACK (on_connection_closed), runtime);
  (void) name;
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  PortalRuntime *runtime = user_data;

  if (connection != runtime->connection)
    return;
  if (runtime->backend)
    ooze_portal_backend_stop (g_steal_pointer (&runtime->backend));
  g_clear_object (&runtime->connection);
  g_main_loop_quit (runtime->loop);
  (void) connection;
  (void) name;
}

static void
on_connection_closed (GDBusConnection *connection,
                      gboolean         remote_peer_vanished,
                      GError          *error,
                      gpointer         user_data)
{
  PortalRuntime *runtime = user_data;

  if (connection != runtime->connection)
    return;
  if (runtime->backend)
    ooze_portal_backend_stop (g_steal_pointer (&runtime->backend));
  g_clear_object (&runtime->connection);
  g_main_loop_quit (runtime->loop);
  (void) connection;
  (void) remote_peer_vanished;
  (void) error;
}

int
main (int argc, char **argv)
{
  guint owner_id;
  GMainLoop *loop;
  PortalRuntime runtime = { 0 };

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.impl.portal.desktop.ooze",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired, NULL, on_name_lost,
                             &runtime, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  runtime.loop = loop;
  g_main_loop_run (loop);
  if (runtime.backend)
    ooze_portal_backend_stop (g_steal_pointer (&runtime.backend));
  g_clear_object (&runtime.connection);
  g_main_loop_unref (loop);
  g_bus_unown_name (owner_id);
  (void) argc;
  (void) argv;
  return 0;
}
