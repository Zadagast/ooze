#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_EAR_TYPE_WINDOW (ooze_ear_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeEarWindow, ooze_ear_window, OOZE_EAR, WINDOW, GtkApplicationWindow)

GtkWidget *ooze_ear_window_new (GtkApplication *app);

G_END_DECLS
