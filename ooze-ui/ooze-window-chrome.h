#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

void ooze_window_install_resize_handles (GtkWindow *window,
                                         GtkWidget *root);

void ooze_window_install_drag (GtkWidget *widget,
                               GtkWindow *window);

G_END_DECLS
