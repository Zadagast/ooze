#pragma once

#include "ooze-plugin.h"

#include <glib.h>

G_BEGIN_DECLS

void ooze_tray_setup (OozePlugin *plugin);
void ooze_tray_dispose (OozePlugin *plugin);
void ooze_tray_layout (OozePlugin *plugin, gfloat panel_width, gfloat panel_height);
gfloat ooze_tray_width (OozePlugin *plugin);

/* Light/Dark: re-resolve named icons + content swap only (no actor rebuild). */
void ooze_tray_refresh_appearance (OozePlugin *plugin);

G_END_DECLS
