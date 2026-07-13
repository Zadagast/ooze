#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * XFCE / Unity appmenu-gtk-module bridge.
 *
 * GTK3 clients load appmenu-gtk-module, which strips in-window menubars and
 * registers them with com.canonical.AppMenu.Registrar (X11 / Xwayland).
 * Wayland-native GtkApplication menubars still use gtk_shell1 export.
 *
 * Inkscape and other classic GtkMenuBar apps must run on Xwayland
 * (GDK_BACKEND=x11) — the module is not Wayland-native.
 */

/* Set GTK_MODULES / UBUNTU_MENUPROXY for this process. */
void ooze_appmenu_setup_environment (void);

/* Forward module/proxy env to a dock / compositor-spawned subprocess. */
void ooze_appmenu_apply_to_launcher (GSubprocessLauncher *launcher);

/*
 * Ooze GTK4 apps: prefer native Wayland even if the session defaults
 * foreign GTK3 apps to X11 for appmenu.
 */
void ooze_appmenu_force_wayland_backend (GSubprocessLauncher *launcher);

/*
 * Foreign / GTK3 appmenu clients: force Xwayland so appmenu-gtk-module
 * can register menus.
 */
void ooze_appmenu_force_x11_backend (GSubprocessLauncher *launcher);

/* Same X11 + module env for g_app_info_launch / launch_default_for_uri. */
void ooze_appmenu_prepare_launch_context (GAppLaunchContext *ctx);

/* Foreign apps: modules + X11 + WhiteSur on a GSubprocessLauncher. */
void ooze_appmenu_apply_foreign_to_launcher (GSubprocessLauncher *launcher);

/* Ooze apps via g_app_info_launch: Wayland, never inherit GTK_THEME. */
void ooze_appmenu_prepare_ooze_launch_context (GAppLaunchContext *ctx);

/* Choose Ooze vs foreign env from GAppInfo id (org.ooze.* → Ooze). */
void ooze_appmenu_prepare_launch_context_for_info (GAppLaunchContext *ctx,
                                                   GAppInfo          *info);

/*
 * Copy environ and inject module/proxy + GDK_BACKEND=x11 for VTE shells
 * so `inkscape` from Command registers menus. Caller owns the result.
 */
char **ooze_appmenu_environ_for_foreign (char **envp);

/* TRUE if libappmenu-gtk3-module.so (or unity-gtk-module) is on disk. */
gboolean ooze_appmenu_module_available (void);

/*
 * Ensure appmenu-registrar is on the session bus. Spawns it once if the
 * binary exists and the name is not owned yet. Safe to call repeatedly.
 */
void ooze_appmenu_ensure_registrar (void);

/*
 * Advertise Gtk/ShellShowsMenubar on the nest X11 display (Xwayland),
 * not the host $DISPLAY. Plugin wires Mutter's Display* via my-xsettings;
 * these helpers track the nest display name for republish on app launch.
 */
void ooze_appmenu_ensure_shell_shows_menubar (void);
void ooze_appmenu_ensure_shell_shows_menubar_on_display (const char *display_name);

G_END_DECLS
