#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_THEMES_PANE (ooze_themes_pane_get_type ())
G_DECLARE_FINAL_TYPE (OozeThemesPane, ooze_themes_pane, OOZE, THEMES_PANE, GtkBox)

GtkWidget *ooze_themes_pane_new (void);

G_END_DECLS
