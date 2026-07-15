#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_ABOUT_TYPE_WINDOW (ooze_about_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeAboutWindow, ooze_about_window, OOZE_ABOUT, WINDOW,
                      OozeApplicationWindow)

GtkWidget *ooze_about_window_new (GtkApplication *app);

G_END_DECLS
