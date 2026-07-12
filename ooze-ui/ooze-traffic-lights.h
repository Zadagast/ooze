#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OOZE_TYPE_TRAFFIC_LIGHTS (ooze_traffic_lights_get_type ())
G_DECLARE_FINAL_TYPE (OozeTrafficLights, ooze_traffic_lights, OOZE, TRAFFIC_LIGHTS, GtkWidget)

OozeTrafficLights *ooze_traffic_lights_new (void);

void ooze_traffic_lights_attach_window (OozeTrafficLights *self,
                                        GtkWindow         *window);

G_END_DECLS
