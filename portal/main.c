#include "portal-backend.h"

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  ooze_portal_backend_start (connection);
  (void) name;
  (void) user_data;
}

int
main (int argc, char **argv)
{
  guint owner_id;
  GMainLoop *loop;

  owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                             "org.freedesktop.impl.portal.desktop.ooze",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired, NULL, NULL, NULL, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);
  g_bus_unown_name (owner_id);
  (void) argc;
  (void) argv;
  return 0;
}
