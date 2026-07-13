#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_DISPLAY_PANE (ooze_display_pane_get_type ())
G_DECLARE_FINAL_TYPE (OozeDisplayPane, ooze_display_pane, OOZE, DISPLAY_PANE, GtkBox)

GtkWidget *ooze_display_pane_new (void);

G_END_DECLS
