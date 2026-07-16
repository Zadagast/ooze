/*
 * org_kde_kwin_appmenu server implementation.
 *
 * Lets native-Wayland clients (Qt via the KDE platform theme,
 * GTK3 via appmenu-gtk-module) announce a com.canonical.dbusmenu
 * service/path per surface, which the global menu then binds.
 */
#pragma once

#include <glib.h>
#include <meta/display.h>
#include <meta/window.h>

G_BEGIN_DECLS

typedef struct _OozeAppmenuWayland OozeAppmenuWayland;

typedef void (*OozeAppmenuWaylandChangedFunc) (gpointer user_data);

OozeAppmenuWayland *ooze_appmenu_wayland_new (MetaDisplay *display);

void ooze_appmenu_wayland_free (OozeAppmenuWayland *self);

void ooze_appmenu_wayland_set_changed_callback (OozeAppmenuWayland            *self,
                                                OozeAppmenuWaylandChangedFunc  callback,
                                                gpointer                       user_data);

/*
 * Resolve the dbusmenu address a client announced for @window.
 * Returned strings are owned by @self and valid until the next change.
 */
gboolean ooze_appmenu_wayland_lookup (OozeAppmenuWayland  *self,
                                      MetaWindow          *window,
                                      const char         **service_out,
                                      const char         **path_out);

G_END_DECLS
