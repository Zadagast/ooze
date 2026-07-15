#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_SHOT_TYPE_WINDOW (ooze_shot_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeShotWindow, ooze_shot_window, OOZE_SHOT, WINDOW,
                      OozeApplicationWindow)

GtkWidget *ooze_shot_window_new (GtkApplication *app);

G_END_DECLS
