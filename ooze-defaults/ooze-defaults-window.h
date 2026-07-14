#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define OOZE_DEFAULTS_TYPE_WINDOW (ooze_defaults_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeDefaultsWindow, ooze_defaults_window, OOZE,
                      DEFAULTS_WINDOW, GtkApplicationWindow)

GtkWidget *ooze_defaults_window_new (GtkApplication *app);

G_END_DECLS
