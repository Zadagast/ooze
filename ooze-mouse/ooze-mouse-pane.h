#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_MOUSE_TYPE_PANE (ooze_mouse_pane_get_type ())
G_DECLARE_FINAL_TYPE (OozeMousePane, ooze_mouse_pane, OOZE,
                      MOUSE_PANE, GtkBox)

GtkWidget *ooze_mouse_pane_new (void);

G_END_DECLS
