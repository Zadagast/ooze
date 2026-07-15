#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_EYE_TYPE_WINDOW (ooze_eye_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeEyeWindow, ooze_eye_window, OOZE_EYE, WINDOW,
                      OozeApplicationWindow)

GtkWidget *ooze_eye_window_new (GtkApplication *app);

/* Load (or replace) the image shown in this window. */
void ooze_eye_window_open_file (OozeEyeWindow *self,
                                GFile         *file);

G_END_DECLS
