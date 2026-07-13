#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Global menus
 * ------------
 * Ooze GTK4 apps use Wayland gtk_shell1 GLOBAL_APP_MENU (always on).
 *
 * Foreign / classic GTK3 AppMenu (Xwayland + appmenu-gtk-module + dbusmenu)
 * is OFF by default — sync registrar/GetLayout on the compositor main thread
 * freezes the session (Inkscape focus). Re-enable only for debugging:
 *   OOZE_FOREIGN_GLOBAL_MENU=1
 */

/* TRUE when OOZE_FOREIGN_GLOBAL_MENU is set to a truthy value (1/true/yes). */
gboolean ooze_appmenu_foreign_enabled (void);

/* Set GTK_MODULES / UBUNTU_MENUPROXY when foreign global menus are enabled. */
void ooze_appmenu_setup_environment (void);

/* Forward module/proxy env to a dock / compositor-spawned subprocess. */
void ooze_appmenu_apply_to_launcher (GSubprocessLauncher *launcher);

/*
 * Ooze GTK4 apps: prefer native Wayland; never inherit foreign GTK_THEME.
 */
void ooze_appmenu_force_wayland_backend (GSubprocessLauncher *launcher);

/*
 * Foreign AppMenu clients (debug only): force Xwayland for appmenu-gtk-module.
 */
void ooze_appmenu_force_x11_backend (GSubprocessLauncher *launcher);

/*
 * Foreign launch via g_app_info_launch.
 * Default: WhiteSur theme only (in-window menus).
 * With OOZE_FOREIGN_GLOBAL_MENU=1: modules + X11 + ShellShowsMenubar.
 */
void ooze_appmenu_prepare_launch_context (GAppLaunchContext *ctx);

/* Foreign apps on GSubprocessLauncher — same gate as prepare_launch_context. */
void ooze_appmenu_apply_foreign_to_launcher (GSubprocessLauncher *launcher);

/* Ooze apps via g_app_info_launch: Wayland, never inherit GTK_THEME. */
void ooze_appmenu_prepare_ooze_launch_context (GAppLaunchContext *ctx);

/* Choose Ooze vs foreign env from GAppInfo id (org.ooze.* → Ooze). */
void ooze_appmenu_prepare_launch_context_for_info (GAppLaunchContext *ctx,
                                                   GAppInfo          *info);

/*
 * Environ for VTE / Command shells. Default: WhiteSur only.
 * Debug flag: also inject modules + GDK_BACKEND=x11.
 * Caller owns the result.
 */
char **ooze_appmenu_environ_for_foreign (char **envp);

/* TRUE if libappmenu-gtk3-module.so (or unity-gtk-module) is on disk. */
gboolean ooze_appmenu_module_available (void);

/*
 * Ensure appmenu-registrar is on the session bus (no-op unless foreign menus on).
 */
void ooze_appmenu_ensure_registrar (void);

/*
 * Advertise Gtk/ShellShowsMenubar on the nest X11 display when foreign menus
 * are enabled. Plugin wires Mutter's Display* via ooze-xsettings.
 */
void ooze_appmenu_ensure_shell_shows_menubar (void);
void ooze_appmenu_ensure_shell_shows_menubar_on_display (const char *display_name);

G_END_DECLS
