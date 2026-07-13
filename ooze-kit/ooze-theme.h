#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeKit theme entry point — call once at app startup (after display init).
 *
 * Sets Inter 11pt regular as the default UI font for the process and loads
 * shared CSS so kit/app chrome stays weight 400 unless marked .ooze-emphasis.
 * Also installs the shared Ooze Gel CSD look (9px corners + Aqua shadow) for
 * windows that opt in with the "spot-finder" CSS class.
 */
void ooze_theme_ensure (void);

G_END_DECLS
