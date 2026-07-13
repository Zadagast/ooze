#include "ooze-shared-appmenu.h"

#include <stdlib.h>
#include <string.h>

/* Provided by compositor/my-xsettings.c only. Spot/Command share this
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
          if (g_strcmp0 (parts[i], APPMENU_MODULE_NAME) == 0 ||
              g_strcmp0 (parts[i], "appmenu-gtk3-module") == 0 ||
              g_strcmp0 (parts[i], "unity-gtk-module") == 0)
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
ooze_appmenu_setup_environment (void)
{
  ooze_appmenu_prepend_module ();

  if (!g_getenv ("UBUNTU_MENUPROXY") || !*g_getenv ("UBUNTU_MENUPROXY"))
    g_setenv ("UBUNTU_MENUPROXY", "1", TRUE);
}

static void
ooze_appmenu_launcher_set_modules (GSubprocessLauncher *launcher)
{
  const char *modules;
  const char *proxy;

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

static gboolean
ooze_appmenu_theme_index_exists (const char *name)
{
  g_autofree char *user_path = NULL;
  g_autofree char *home_path = NULL;
  g_autofree char *system_path = NULL;

  if (!name || !*name)
    return FALSE;

  user_path = g_build_filename (g_get_user_data_dir (), "themes", name,
                                "index.theme", NULL);
  if (g_file_test (user_path, G_FILE_TEST_EXISTS))
    return TRUE;

  home_path = g_build_filename (g_get_home_dir (), ".themes", name,
                                "index.theme", NULL);
  if (g_file_test (home_path, G_FILE_TEST_EXISTS))
    return TRUE;

  system_path = g_build_filename ("/usr/share/themes", name, "index.theme", NULL);
  return g_file_test (system_path, G_FILE_TEST_EXISTS);
}

static const char *
ooze_appmenu_foreign_gtk_theme (void)
{
  g_autoptr (GSettings) iface = NULL;
  gboolean dark = FALSE;
  const char *name;

  iface = g_settings_new ("org.gnome.desktop.interface");
  if (iface)
    {
      g_autofree char *scheme = g_settings_get_string (iface, "color-scheme");

      dark = (g_strcmp0 (scheme, "prefer-dark") == 0);
    }

  name = dark ? "WhiteSur-Dark" : "WhiteSur-Light";
  if (ooze_appmenu_theme_index_exists (name))
    return name;

  name = dark ? "WhiteSur-Light" : "WhiteSur-Dark";
  if (ooze_appmenu_theme_index_exists (name))
    return name;

  return NULL;
}

void
ooze_appmenu_force_x11_backend (GSubprocessLauncher *launcher)
{
  const char *theme;

  g_return_if_fail (launcher != NULL);
  g_subprocess_launcher_setenv (launcher, "GDK_BACKEND", "x11", TRUE);
  theme = ooze_appmenu_foreign_gtk_theme ();
  if (theme)
    g_subprocess_launcher_setenv (launcher, "GTK_THEME", theme, TRUE);
}

void
ooze_appmenu_prepare_launch_context (GAppLaunchContext *ctx)
{
  const char *modules;
  const char *proxy;
  const char *theme;

  g_return_if_fail (G_IS_APP_LAUNCH_CONTEXT (ctx));

  ooze_appmenu_setup_environment ();

  modules = g_getenv ("GTK_MODULES");
  if (modules && *modules)
    g_app_launch_context_setenv (ctx, "GTK_MODULES", modules);

  proxy = g_getenv ("UBUNTU_MENUPROXY");
  if (proxy && *proxy)
    g_app_launch_context_setenv (ctx, "UBUNTU_MENUPROXY", proxy);

  /* Classic GtkMenuBar exporters only speak AppMenu on X11. */
  g_app_launch_context_setenv (ctx, "GDK_BACKEND", "x11");
  theme = ooze_appmenu_foreign_gtk_theme ();
  if (theme)
    g_app_launch_context_setenv (ctx, "GTK_THEME", theme);
  ooze_appmenu_ensure_shell_shows_menubar ();
}

void
ooze_appmenu_apply_foreign_to_launcher (GSubprocessLauncher *launcher)
{
  g_return_if_fail (launcher != NULL);

  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_x11_backend (launcher);
}

void
ooze_appmenu_prepare_ooze_launch_context (GAppLaunchContext *ctx)
{
  g_return_if_fail (G_IS_APP_LAUNCH_CONTEXT (ctx));

  ooze_appmenu_setup_environment ();
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
  const char *modules;
  const char *proxy;
  const char *theme;

  ooze_appmenu_setup_environment ();
  modules = g_getenv ("GTK_MODULES");
  proxy = g_getenv ("UBUNTU_MENUPROXY");

  if (!envp)
    envp = g_get_environ ();

  if (modules && *modules)
    envp = g_environ_setenv (envp, "GTK_MODULES", modules, TRUE);
  if (proxy && *proxy)
    envp = g_environ_setenv (envp, "UBUNTU_MENUPROXY", proxy, TRUE);
  envp = g_environ_setenv (envp, "GDK_BACKEND", "x11", TRUE);
  theme = ooze_appmenu_foreign_gtk_theme ();
  if (theme)
    envp = g_environ_setenv (envp, "GTK_THEME", theme, TRUE);
  ooze_appmenu_ensure_shell_shows_menubar ();
  return envp;
}

void
ooze_appmenu_ensure_registrar (void)
{
  g_autoptr (GDBusConnection) session = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) owner = NULL;
  g_autoptr (GVariant) started = NULL;
  guint32 result = 0;

  session = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!session)
    {
      g_warning ("Ooze appmenu: session bus unavailable: %s",
                 error ? error->message : "unknown");
      return;
    }

  owner = g_dbus_connection_call_sync (session,
                                       "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "GetNameOwner",
                                       g_variant_new ("(s)", APPMENU_REGISTRAR_NAME),
                                       G_VARIANT_TYPE ("(s)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       500,
                                       NULL,
                                       NULL);
  if (owner)
    return;

  /* Prefer D-Bus activation (one instance) over spawning by hand — a
   * second appmenu-registrar fails with org.valapanel.AppMenu.Registrar. */
  error = NULL;
  started = g_dbus_connection_call_sync (session,
                                         "org.freedesktop.DBus",
                                         "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus",
                                         "StartServiceByName",
                                         g_variant_new ("(su)",
                                                        APPMENU_REGISTRAR_NAME,
                                                        0u),
                                         G_VARIANT_TYPE ("(u)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         3000,
                                         NULL,
                                         &error);
  if (started)
    {
      g_variant_get (started, "(u)", &result);
      if (result == 1 || result == 2) /* started or already running */
        {
          g_print ("Ooze appmenu: AppMenu registrar active (D-Bus)\n");
          return;
        }
    }

  g_warning ("Ooze appmenu: could not activate %s: %s "
             "(install appmenu-registrar / run scripts/install-appmenu.sh)",
             APPMENU_REGISTRAR_NAME,
             error ? error->message : "unknown");
}

static guint xsettingsd_retry_id = 0;
static char *xsettings_target_display = NULL;
static char *xsettings_started_display = NULL;

static gboolean
ooze_appmenu_try_start_xsettingsd (gpointer user_data G_GNUC_UNUSED)
{
  static guint retries;
  const char *display;

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
      /* Refresh so Inkscape started mid-session still sees ShellShowsMenubar. */
      if (ooze_xsettings_republish != NULL)
        ooze_xsettings_republish ();
      xsettingsd_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  /*
   * Prefer the built-in nest XSETTINGS manager when linked into the compositor.
   * Spot/Command/Eye must NOT start system xsettingsd — that steals
   * _XSETTINGS_S0 from the nest and drops WhiteSur ThemeName.
   */
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
