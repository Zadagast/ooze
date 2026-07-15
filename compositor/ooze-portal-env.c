#include "ooze-portal-env.h"

#include <gio/gio.h>

/*
 * xdg-desktop-portal-gtk (the Settings backend every GTK app queries for
 * light/dark at startup) is a systemd --user service. It can only connect
 * to the session's display if WAYLAND_DISPLAY / DISPLAY are present in the
 * activation environment — and those only exist once this compositor has
 * created them. Publish them from here, then kick the portal units so they
 * start against a live display instead of crash-looping.
 */

#define OOZE_PORTAL_ENV_TIMEOUT_MS 5000

static const char *const publish_vars[] = {
  "WAYLAND_DISPLAY",
  "DISPLAY",
  "XAUTHORITY",
};

static const char *const portal_units[] = {
  "xdg-desktop-portal-gtk.service",
  "xdg-desktop-portal.service",
};

static void
ooze_portal_env_call_done (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  char *what = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                         res, &error);
  if (error)
    g_warning ("Ooze portal env: %s failed: %s", what, error->message);

  g_free (what);
}

static void
ooze_portal_env_kick_portals (GDBusConnection *conn)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (portal_units); i++)
    {
      const char *unit = portal_units[i];

      g_dbus_connection_call (conn,
                              "org.freedesktop.systemd1",
                              "/org/freedesktop/systemd1",
                              "org.freedesktop.systemd1.Manager",
                              "ResetFailedUnit",
                              g_variant_new ("(s)", unit),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              OOZE_PORTAL_ENV_TIMEOUT_MS,
                              NULL,
                              NULL,
                              NULL);
      g_dbus_connection_call (conn,
                              "org.freedesktop.systemd1",
                              "/org/freedesktop/systemd1",
                              "org.freedesktop.systemd1.Manager",
                              "RestartUnit",
                              g_variant_new ("(ss)", unit, "replace"),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              OOZE_PORTAL_ENV_TIMEOUT_MS,
                              NULL,
                              ooze_portal_env_call_done,
                              g_strdup_printf ("RestartUnit %s", unit));
    }
}

static void
ooze_portal_env_bus_got (GObject      *source G_GNUC_UNUSED,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  gboolean kick = GPOINTER_TO_INT (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) conn = NULL;
  GVariantBuilder dict;
  GVariantBuilder list;
  gboolean have_vars = FALSE;
  gsize i;

  conn = g_bus_get_finish (res, &error);
  if (!conn)
    {
      g_warning ("Ooze portal env: session bus unavailable: %s",
                 error->message);
      return;
    }

  g_variant_builder_init (&dict, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_init (&list, G_VARIANT_TYPE ("as"));

  for (i = 0; i < G_N_ELEMENTS (publish_vars); i++)
    {
      const char *name = publish_vars[i];
      const char *value = g_getenv (name);
      g_autofree char *assignment = NULL;

      if (!value || !*value)
        continue;

      g_variant_builder_add (&dict, "{ss}", name, value);
      assignment = g_strdup_printf ("%s=%s", name, value);
      g_variant_builder_add (&list, "s", assignment);
      have_vars = TRUE;
    }

  if (!have_vars)
    {
      g_variant_builder_clear (&dict);
      g_variant_builder_clear (&list);
      return;
    }

  g_dbus_connection_call (conn,
                          "org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "UpdateActivationEnvironment",
                          g_variant_new ("(a{ss})", &dict),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          OOZE_PORTAL_ENV_TIMEOUT_MS,
                          NULL,
                          ooze_portal_env_call_done,
                          g_strdup ("UpdateActivationEnvironment"));

  g_dbus_connection_call (conn,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "SetEnvironment",
                          g_variant_new ("(as)", &list),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          OOZE_PORTAL_ENV_TIMEOUT_MS,
                          NULL,
                          ooze_portal_env_call_done,
                          g_strdup ("systemd SetEnvironment"));

  /*
   * Method calls to the same peer on one connection are dispatched in
   * order, so the units restart after SetEnvironment has been applied.
   */
  if (kick)
    ooze_portal_env_kick_portals (conn);
}

void
ooze_portal_env_publish (void)
{
  static gboolean kicked;
  gboolean kick;

  /* Nested devkit shares the host session: never touch its portals. */
  if (g_strcmp0 (g_getenv ("OOZE_DEVKIT"), "1") == 0)
    return;

  kick = !kicked;
  kicked = TRUE;

  g_bus_get (G_BUS_TYPE_SESSION, NULL,
             ooze_portal_env_bus_got, GINT_TO_POINTER (kick));
}
