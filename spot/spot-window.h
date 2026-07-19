#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define SPOT_TYPE_WINDOW (spot_window_get_type ())
G_DECLARE_FINAL_TYPE (SpotWindow, spot_window, SPOT, WINDOW,
                      OozeApplicationWindow)

SpotWindow *spot_window_new (GtkApplication *app);

SpotWindow *spot_window_new_for_path (GtkApplication *app,
                                      const char     *path);

void spot_window_open_path (SpotWindow *self,
                            const char *path);

void spot_window_reveal_uri (SpotWindow   *self,
                             const char   *uri);

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
