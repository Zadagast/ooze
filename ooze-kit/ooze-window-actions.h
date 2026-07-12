#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Register standard win.* actions on an application window:
 *   close, minimize, maximize
 *   cut, copy, paste, select-all  (focused-widget clipboard / selection)
 *
 * Apps with custom clipboard (Spot files, VTE) should register their own
 * cut/copy/paste and only call ooze_window_actions_add_chrome().
 */
void ooze_window_actions_add_chrome (GtkApplicationWindow *window);
void ooze_window_actions_add_edit   (GtkApplicationWindow *window);

/* Append Edit / Window submenus that bind to the actions above. */
void ooze_menubar_append_edit   (GMenu *bar);
void ooze_menubar_append_window (GMenu *bar);

G_END_DECLS
