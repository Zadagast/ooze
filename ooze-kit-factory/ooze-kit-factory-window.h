#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_KIT_FACTORY_WINDOW (ooze_kit_factory_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeKitFactoryWindow,
                      ooze_kit_factory_window,
                      OOZE, KIT_FACTORY_WINDOW,
                      GtkApplicationWindow)

GtkWidget *ooze_kit_factory_window_new (GtkApplication *app);

G_END_DECLS
