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

/* Receive files into the window's current folder (D-Bus / shell bridge). */
void spot_window_receive_paths (SpotWindow         *self,
                                const char * const *paths,
                                gboolean            prefer_move);

/* Compositor shell-drag: highlight + spring while desktop owns the pointer. */
void spot_window_shell_drag_motion (SpotWindow *self,
                                    double      x,
                                    double      y);
void spot_window_shell_drag_leave (SpotWindow *self);

G_END_DECLS
