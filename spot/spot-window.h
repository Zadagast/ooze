#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SPOT_TYPE_WINDOW (spot_window_get_type ())
G_DECLARE_FINAL_TYPE (SpotWindow, spot_window, SPOT, WINDOW, GtkApplicationWindow)

SpotWindow *spot_window_new (AdwApplication *app);

SpotWindow *spot_window_new_for_path (AdwApplication *app,
                                      const char    *path);

void spot_window_open_path (SpotWindow *self,
                            const char *path);

G_END_DECLS
