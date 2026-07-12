#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_COMMAND_WINDOW (ooze_command_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeCommandWindow, ooze_command_window,
                      OOZE, COMMAND_WINDOW, GtkApplicationWindow)

GtkWidget *ooze_command_window_new (GtkApplication *app);

G_END_DECLS
