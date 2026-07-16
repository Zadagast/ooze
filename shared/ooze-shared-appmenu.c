#include "ooze-shared-appmenu.h"
#include "ooze-foreign-gtk.h"

#include <stdlib.h>
#include <string.h>

/* Provided by compositor/ooze-xsettings.c only. Spot/Command share this
 * file for launch env and must not own the nest XSETTINGS selection. */
gboolean ooze_xsettings_ensure_shell_shows_menubar (const char *display_name)
  __attribute__ ((weak));
gboolean ooze_xsettings_ensure_with_xdisplay (void       *dpy,
                                            const char *display_name,
                                            gboolean    owns_connection)
  __attribute__ ((weak));
void ooze_xsettings_republish (void) __attribute__ ((weak));

#define APPMENU_MODULE_NAME "appmenu-gtk-module"
#define APPMENU_REGISTRAR_NAME "com.canonical.AppMenu.Registrar"
#define APPMENU_REGISTRAR_PATH "/com/canonical/AppMenu/Registrar"

typedef struct
{
  GDBusConnection *session;
} OozeAppmenuRegistrarActivation;

static void
ooze_appmenu_registrar_activation_free (
  OozeAppmenuRegistrarActivation *activation)
{
  if (!activation)
    return;
  g_clear_object (&activation->session);
  g_free (activation);
}

static void
ooze_appmenu_registrar_start_cb (GObject      *source,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  OozeAppmenuRegistrarActivation *activation = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  guint32 result = 0;
  gboolean active = FALSE;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (reply)
    {
      g_variant_get (reply, "(u)", &result);
      if (result == 1 || result == 2)
        {
          g_print ("Ooze appmenu: AppMenu registrar active (D-Bus)\n");
          active = TRUE;
        }
      else
        {
          error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                               "StartServiceByName returned %u", result);
        }
    }

  if (!active)
    g_warning ("Ooze appmenu: could not activate %s: %s "
               "(install appmenu-registrar / run scripts/install-appmenu.sh)",
               APPMENU_REGISTRAR_NAME,
               error ? error->message : "unknown");

  ooze_appmenu_registrar_activation_free (activation);
}

static void
ooze_appmenu_registrar_owner_cb (GObject      *source,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  OozeAppmenuRegistrarActivation *activation = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) owner = NULL;

  owner = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (owner)
    {
      ooze_appmenu_registrar_activation_free (activation);
      return;
    }

  g_dbus_connection_call (activation->session,
                          "org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "StartServiceByName",
                          g_variant_new ("(su)", APPMENU_REGISTRAR_NAME, 0u),
                          G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          3000,
                          NULL,
                          ooze_appmenu_registrar_start_cb,
                          activation);
}

static void
ooze_appmenu_registrar_bus_ready_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  OozeAppmenuRegistrarActivation *activation = user_data;
  g_autoptr (GError) error = NULL;

  activation->session = g_bus_get_finish (res, &error);
  if (!activation->session)
    {
      g_warning ("Ooze appmenu: session bus unavailable: %s",
                 error ? error->message : "unknown");
      ooze_appmenu_registrar_activation_free (activation);
      return;
    }

  g_dbus_connection_call (activation->session,
                          "org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "GetNameOwner",
                          g_variant_new ("(s)", APPMENU_REGISTRAR_NAME),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          500,
                          NULL,
                          ooze_appmenu_registrar_owner_cb,
                          activation);
  (void) source;
}

static gboolean
ooze_appmenu_module_is_managed (const char *module)
{
  return g_strcmp0 (module, "appmenu-gtk-module") == 0 ||
         g_strcmp0 (module, "appmenu-gtk3-module") == 0 ||
         g_strcmp0 (module, "unity-gtk-module") == 0 ||
         g_strcmp0 (module, "unity-gtk3-module") == 0;
}

gboolean
ooze_appmenu_foreign_enabled (void)
{
  const char *v = g_getenv ("OOZE_FOREIGN_GLOBAL_MENU");

  if (!v || !*v)
    return FALSE;

  return g_ascii_strcasecmp (v, "1") == 0 ||
         g_ascii_strcasecmp (v, "true") == 0 ||
         g_ascii_strcasecmp (v, "yes") == 0 ||
         g_ascii_strcasecmp (v, "on") == 0;
}

static gboolean
ooze_appmenu_module_on_disk (void)
{
  static const char *const candidates[] = {
    "/usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk-module.so",
    "/usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk3-module.so",
    "/usr/lib/gtk-3.0/modules/libappmenu-gtk-module.so",
    "/usr/lib/gtk-3.0/modules/libappmenu-gtk3-module.so",
    "/usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libunity-gtk3-module.so",
    NULL
  };
  int i;

  for (i = 0; candidates[i]; i++)
    {
      if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
        return TRUE;
    }

  return FALSE;
}

gboolean
ooze_appmenu_module_available (void)
{
  return ooze_appmenu_module_on_disk ();
}

static void
ooze_appmenu_prepend_module (void)
{
  const char *existing;
  g_autofree char *next = NULL;

  existing = g_getenv ("GTK_MODULES");
  if (existing && *existing)
    {
      g_auto (GStrv) parts = g_strsplit (existing, ":", -1);
      int i;

      for (i = 0; parts && parts[i]; i++)
        {
          if (ooze_appmenu_module_is_managed (parts[i]))
            return;
        }

      next = g_strdup_printf ("%s:%s", APPMENU_MODULE_NAME, existing);
    }
  else
    {
      next = g_strdup (APPMENU_MODULE_NAME);
    }

  g_setenv ("GTK_MODULES", next, TRUE);
}

void
ooze_appmenu_strip_modules (void)
{
  const char *existing;
  g_autofree char *cleaned = NULL;
  g_auto (GStrv) parts = NULL;
  GString *result;
  int i;

  if (ooze_appmenu_foreign_enabled ())
    return;

  existing = g_getenv ("GTK_MODULES");
  if (!existing || !*existing)
    {
      g_unsetenv ("GTK_MODULES");
      return;
    }

  parts = g_strsplit (existing, ":", -1);
  result = g_string_new (NULL);

  for (i = 0; parts && parts[i]; i++)
    {
      if (!parts[i][0] || ooze_appmenu_module_is_managed (parts[i]))
        continue;

      if (result->len > 0)
        g_string_append_c (result, ':');
      g_string_append (result, parts[i]);
    }

  cleaned = g_string_free (result, FALSE);
  if (cleaned[0] != '\0')
    g_setenv ("GTK_MODULES", cleaned, TRUE);
  else
    g_unsetenv ("GTK_MODULES");
}

void
ooze_appmenu_setup_environment (void)
{
  if (!ooze_appmenu_foreign_enabled ())
    {
      ooze_appmenu_strip_modules ();
      return;
    }

  ooze_appmenu_prepend_module ();

  if (!g_getenv ("UBUNTU_MENUPROXY") || !*g_getenv ("UBUNTU_MENUPROXY"))
    g_setenv ("UBUNTU_MENUPROXY", "1", TRUE);
}

static void
ooze_appmenu_launcher_set_modules (GSubprocessLauncher *launcher)
{
  const char *modules;
  const char *proxy;

  if (!ooze_appmenu_foreign_enabled ())
    return;

  modules = g_getenv ("GTK_MODULES");
  if (modules && *modules)
    g_subprocess_launcher_setenv (launcher, "GTK_MODULES", modules, TRUE);

  proxy = g_getenv ("UBUNTU_MENUPROXY");
  if (proxy && *proxy)
    g_subprocess_launcher_setenv (launcher, "UBUNTU_MENUPROXY", proxy, TRUE);
}

void
ooze_appmenu_apply_to_launcher (GSubprocessLauncher *launcher)
{
  g_return_if_fail (launcher != NULL);

  if (!ooze_appmenu_foreign_enabled ())
    return;

  ooze_appmenu_setup_environment ();
  ooze_appmenu_launcher_set_modules (launcher);
  ooze_appmenu_ensure_shell_shows_menubar ();
}

void
ooze_appmenu_force_wayland_backend (GSubprocessLauncher *launcher)
{
  g_return_if_fail (launcher != NULL);
  g_subprocess_launcher_setenv (launcher, "GDK_BACKEND", "wayland", TRUE);
  /* Never inherit a foreign Mac GTK skin into Ooze apps. */
  g_subprocess_launcher_unsetenv (launcher, "GTK_THEME");
}

void
ooze_appmenu_force_x11_backend (GSubprocessLauncher *launcher)
{
  g_return_if_fail (launcher != NULL);
  g_subprocess_launcher_setenv (launcher, "GDK_BACKEND", "x11", TRUE);
}

static void
ooze_appmenu_set_foreign_theme (GAppLaunchContext *ctx)
{
  g_autofree char *theme = ooze_foreign_gtk_theme_for_session ();

  if (theme)
    g_app_launch_context_setenv (ctx, "GTK_THEME", theme);
}

void
ooze_appmenu_prepare_launch_context (GAppLaunchContext *ctx)
{
  g_return_if_fail (G_IS_APP_LAUNCH_CONTEXT (ctx));

  /* Xwayland so XSETTINGS can flip WhiteSur-Light↔Dark live. AppMenu modules
   * stay off unless OOZE_FOREIGN_GLOBAL_MENU=1. */
  g_app_launch_context_setenv (ctx, "GDK_BACKEND", "x11");
  ooze_appmenu_set_foreign_theme (ctx);

  if (!ooze_appmenu_foreign_enabled ())
    return;

  ooze_appmenu_setup_environment ();

  {
    const char *modules = g_getenv ("GTK_MODULES");
    const char *proxy = g_getenv ("UBUNTU_MENUPROXY");

    if (modules && *modules)
      g_app_launch_context_setenv (ctx, "GTK_MODULES", modules);
    if (proxy && *proxy)
      g_app_launch_context_setenv (ctx, "UBUNTU_MENUPROXY", proxy);
  }

  ooze_appmenu_ensure_shell_shows_menubar ();
}

void
ooze_appmenu_apply_foreign_to_launcher (GSubprocessLauncher *launcher)
{
  g_return_if_fail (launcher != NULL);

  /* X11 always; modules only when foreign AppMenu debug is on. */
  ooze_appmenu_force_x11_backend (launcher);

  if (!ooze_appmenu_foreign_enabled ())
    return;

  ooze_appmenu_apply_to_launcher (launcher);
}

void
ooze_appmenu_prepare_ooze_launch_context (GAppLaunchContext *ctx)
{
  g_return_if_fail (G_IS_APP_LAUNCH_CONTEXT (ctx));

  /* Native gtk_shell1 menus — no appmenu module. */
  g_app_launch_context_setenv (ctx, "GDK_BACKEND", "wayland");
  g_app_launch_context_unsetenv (ctx, "GTK_THEME");
}

void
ooze_appmenu_prepare_launch_context_for_info (GAppLaunchContext *ctx,
                                              GAppInfo          *info)
{
  const char *id;

  g_return_if_fail (G_IS_APP_LAUNCH_CONTEXT (ctx));

  id = info ? g_app_info_get_id (info) : NULL;
  if (id && g_str_has_prefix (id, "org.ooze."))
    ooze_appmenu_prepare_ooze_launch_context (ctx);
  else
    ooze_appmenu_prepare_launch_context (ctx);
}

char **
ooze_appmenu_environ_for_foreign (char **envp)
{
  if (!envp)
    envp = g_get_environ ();

  /* Xwayland for live Appearance via XSETTINGS (menus still default off). */
  envp = g_environ_setenv (envp, "GDK_BACKEND", "x11", TRUE);
  {
    g_autofree char *theme = ooze_foreign_gtk_theme_for_session ();

    if (theme)
      envp = g_environ_setenv (envp, "GTK_THEME", theme, TRUE);
  }

  if (!ooze_appmenu_foreign_enabled ())
    return envp;

  ooze_appmenu_setup_environment ();
  {
    const char *modules = g_getenv ("GTK_MODULES");
    const char *proxy = g_getenv ("UBUNTU_MENUPROXY");

    if (modules && *modules)
      envp = g_environ_setenv (envp, "GTK_MODULES", modules, TRUE);
    if (proxy && *proxy)
      envp = g_environ_setenv (envp, "UBUNTU_MENUPROXY", proxy, TRUE);
  }
  ooze_appmenu_ensure_shell_shows_menubar ();
  return envp;
}

void
ooze_appmenu_ensure_registrar (void)
{
  OozeAppmenuRegistrarActivation *activation;

  if (!ooze_appmenu_foreign_enabled ())
    return;

  activation = g_new0 (OozeAppmenuRegistrarActivation, 1);
  g_bus_get (G_BUS_TYPE_SESSION, NULL,
             ooze_appmenu_registrar_bus_ready_cb, activation);
}

static guint xsettingsd_retry_id = 0;
static char *xsettings_target_display = NULL;
static char *xsettings_started_display = NULL;

static gboolean
ooze_appmenu_try_start_xsettingsd (gpointer user_data G_GNUC_UNUSED)
{
  static guint retries;
  const char *display;

  if (!ooze_appmenu_foreign_enabled ())
    {
      xsettingsd_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  display = xsettings_target_display;
  if (!display || !*display)
    display = g_getenv ("DISPLAY");

  if (!display || !*display)
    {
      if (retries++ < 30)
        return G_SOURCE_CONTINUE;
      xsettingsd_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  if (g_strcmp0 (xsettings_started_display, display) == 0)
    {
      if (ooze_xsettings_republish != NULL)
        ooze_xsettings_republish ();
      xsettingsd_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  if (ooze_xsettings_ensure_shell_shows_menubar != NULL)
    {
      if (ooze_xsettings_ensure_shell_shows_menubar (display))
        {
          g_free (xsettings_started_display);
          xsettings_started_display = g_strdup (display);
          xsettingsd_retry_id = 0;
          return G_SOURCE_REMOVE;
        }

      if (retries++ < 30)
        return G_SOURCE_CONTINUE;

      {
        static gboolean warned;

        if (!warned)
          {
            g_warning ("Ooze appmenu: could not advertise ShellShowsMenubar; "
                       "in-window GTK3 menus may remain");
            warned = TRUE;
          }
      }
    }

  xsettingsd_retry_id = 0;
  return G_SOURCE_REMOVE;
}

void
ooze_appmenu_ensure_shell_shows_menubar_on_display (const char *display_name)
{
  if (!ooze_appmenu_foreign_enabled ())
    return;

  if (display_name && *display_name)
    {
      g_free (xsettings_target_display);
      xsettings_target_display = g_strdup (display_name);
    }

  if (xsettingsd_retry_id != 0)
    return;

  if (ooze_appmenu_try_start_xsettingsd (NULL) == G_SOURCE_CONTINUE)
    xsettingsd_retry_id = g_timeout_add_seconds (1, ooze_appmenu_try_start_xsettingsd, NULL);
}

void
ooze_appmenu_ensure_shell_shows_menubar (void)
{
  ooze_appmenu_ensure_shell_shows_menubar_on_display (NULL);
}
