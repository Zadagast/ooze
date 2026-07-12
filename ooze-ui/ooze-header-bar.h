#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_HEADER_BAR (ooze_header_bar_get_type ())
G_DECLARE_FINAL_TYPE (OozeHeaderBar, ooze_header_bar, OOZE, HEADER_BAR, GtkBox)

OozeHeaderBar *ooze_header_bar_new (void);

void ooze_header_bar_attach_window (OozeHeaderBar *self,
                                    GtkWindow     *window);

void ooze_header_bar_set_title (OozeHeaderBar *self,
                                const char    *title);

void ooze_header_bar_set_title_widget (OozeHeaderBar *self,
                                       GtkWidget     *widget);

G_END_DECLS
