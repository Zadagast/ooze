#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_MOUSE_TYPE_WINDOW (ooze_mouse_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeMouseWindow, ooze_mouse_window, OOZE,
                      MOUSE_WINDOW, OozeApplicationWindow)

GtkWidget *ooze_mouse_window_new (GtkApplication *app);

G_END_DECLS
