#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SPOT_TYPE_WINDOW (spot_window_get_type ())
G_DECLARE_FINAL_TYPE (SpotWindow, spot_window, SPOT, WINDOW, GtkApplicationWindow)

SpotWindow *spot_window_new (AdwApplication *app);

SpotWindow *spot_window_new_for_path (AdwApplication *app,
                                      const char    *path);

/* Install the Spot menubar on the application (call from startup). */
void spot_application_setup_menubar (GtkApplication *app);

void spot_window_open_path (SpotWindow *self,
                            const char *path);

G_END_DECLS
