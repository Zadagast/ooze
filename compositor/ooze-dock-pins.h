#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Ordered list of pinned app-ids (e.g. "org.ooze.Spot"). Caller frees with
 * g_strfreev(). Never includes Trash — that stays fixed at the dock end. */
char **ooze_dock_pins_load (void);

void ooze_dock_pins_save (char **app_ids);

/* Defaults used when no config file exists yet. */
const char * const *ooze_dock_pins_defaults (void);

G_END_DECLS
