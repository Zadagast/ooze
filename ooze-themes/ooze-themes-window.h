#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define OOZE_TYPE_THEMES_WINDOW (ooze_themes_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeThemesWindow, ooze_themes_window, OOZE, THEMES_WINDOW, GtkApplicationWindow)

GtkWidget *ooze_themes_window_new (GtkApplication *app);

G_END_DECLS
