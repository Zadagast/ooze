#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Full chrome wrap (shadow + visible resize border). Optional for framed demos. */
void ooze_window_install_resize_handles (GtkWindow *window,
                                         GtkWidget *root);

/*
 * Invisible mid-edge / corner resize grips over an already-built window.
 * Safe to call after gtk_window_set_child(); no-ops if already installed.
 */
void ooze_window_install_edge_resize (GtkWindow *window);

void ooze_window_install_drag (GtkWidget *widget,
                               GtkWindow *window);

G_END_DECLS
