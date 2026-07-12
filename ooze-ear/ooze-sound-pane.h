#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_SOUND_PANE (ooze_sound_pane_get_type ())
G_DECLARE_FINAL_TYPE (OozeSoundPane, ooze_sound_pane, OOZE, SOUND_PANE, GtkBox)

GtkWidget *ooze_sound_pane_new (void);

G_END_DECLS
