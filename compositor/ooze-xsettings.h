#pragma once

#include <glib.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS

/*
 * Nest XSETTINGS manager: GTK theme, icon theme, dark preference,
 * Gtk/ShellShowsMenubar, Gtk/ShellShowsAppmenu, and Gtk/DecorationLayout.
 *
 * Prefer ooze_xsettings_ensure_with_xdisplay() on Mutter's MetaX11Display
 * connection — a second XOpenDisplay to nest Xwayland can deadlock.
 */
gboolean ooze_xsettings_ensure_with_xdisplay (Display    *dpy,
                                            const char *display_name,
                                            gboolean    owns_connection);

gboolean ooze_xsettings_ensure_shell_shows_menubar (const char *display_name);

void ooze_xsettings_republish (void);

/* Forward SelectionRequest / SelectionClear from Mutter's X event hook. */
void ooze_xsettings_handle_xevent (XEvent *event);

G_END_DECLS
