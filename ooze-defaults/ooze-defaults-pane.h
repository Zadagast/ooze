#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_DEFAULTS_TYPE_PANE (ooze_defaults_pane_get_type ())
G_DECLARE_FINAL_TYPE (OozeDefaultsPane, ooze_defaults_pane, OOZE,
                      DEFAULTS_PANE, GtkBox)

GtkWidget *ooze_defaults_pane_new (void);

G_END_DECLS
