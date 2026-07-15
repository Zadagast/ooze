#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_KING_TYPE_WINDOW (ooze_king_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeKingWindow, ooze_king_window, OOZE_KING, WINDOW,
                      OozeApplicationWindow)

GtkWidget *ooze_king_window_new (GtkApplication *app);

G_END_DECLS
