/* compositor-internal only — not for GTK apps */
#pragma once

#include "ooze-plugin-priv.h"

G_BEGIN_DECLS

/*
 * Experimental: draw Ooze Gel traffic lights over foreign Wayland CSD windows
 * (libadwaita/GTK4 apps that render their own title bar). Ooze forces their
 * window controls to the empty left slot via the decoration layout, and this
 * paints Aqua close/minimize/zoom lights there, wiring clicks to
 * delete/minimize/(un)maximize.
 *
 * Off by default; enable with OOZE_FOREIGN_GEL=1. No-op for X11 windows
 * (they get real Mutter frames) and for Ooze's own apps (already Gel).
 */
void ooze_foreign_gel_init (OozePlugin *plugin);
void ooze_foreign_gel_shutdown (OozePlugin *plugin);

/* Whether the foreign Gel overlay is enabled for this session. */
gboolean ooze_foreign_gel_enabled (void);

G_END_DECLS
