#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_SCENERY_TYPE_WINDOW (ooze_scenery_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeSceneryWindow, ooze_scenery_window, OOZE_SCENERY,
                      WINDOW, OozeApplicationWindow)

GtkWidget *ooze_scenery_window_new (GtkApplication *app);

G_END_DECLS
