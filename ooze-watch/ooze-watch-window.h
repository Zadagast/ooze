#pragma once

#include "ooze-application-window.h"

#include <gtk/gtk.h>

#define OOZE_WATCH_TYPE_WINDOW (ooze_watch_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeWatchWindow, ooze_watch_window, OOZE_WATCH,
                      WINDOW, OozeApplicationWindow)

GtkWidget *ooze_watch_window_new (GtkApplication *app);

void ooze_watch_window_open_file (OozeWatchWindow *self, GFile *file);
