#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_TYPE_MONITOR_WINDOW (ooze_monitor_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeMonitorWindow, ooze_monitor_window, OOZE,
                      MONITOR_WINDOW, OozeApplicationWindow)

GtkWidget *ooze_monitor_window_new (GtkApplication *app);

G_END_DECLS
