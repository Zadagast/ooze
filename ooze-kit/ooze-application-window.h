#pragma once

#include "ooze-header-bar.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_APPLICATION_WINDOW (ooze_application_window_get_type ())
G_DECLARE_DERIVABLE_TYPE (OozeApplicationWindow, ooze_application_window, OOZE,
                          APPLICATION_WINDOW, GtkApplicationWindow)

struct _OozeApplicationWindowClass
{
  GtkApplicationWindowClass parent_class;
};

OozeApplicationWindow *ooze_application_window_new (GtkApplication *application);

void ooze_application_window_set_content (OozeApplicationWindow *self,
                                          GtkWidget             *content);

void ooze_application_window_set_title (OozeApplicationWindow *self,
                                        const char            *title);

void ooze_application_window_set_title_widget (OozeApplicationWindow *self,
                                               GtkWidget             *widget);

OozeHeaderBar *ooze_application_window_get_header_bar
  (OozeApplicationWindow *self);

void ooze_application_window_append_menu_section (OozeApplicationWindow *self,
                                                  const char            *label,
                                                  GMenuModel            *section);

void ooze_application_window_append_standard_edit_menu
  (OozeApplicationWindow *self);

void ooze_application_window_append_standard_window_menu
  (OozeApplicationWindow *self);

G_END_DECLS
