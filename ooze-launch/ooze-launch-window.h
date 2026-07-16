#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_LAUNCH_TYPE_WINDOW (ooze_launch_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeLaunchWindow, ooze_launch_window, OOZE_LAUNCH,
                      WINDOW, OozeApplicationWindow)

GtkWidget *ooze_launch_window_new (GtkApplication *app);

G_END_DECLS
