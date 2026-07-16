#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Launch XDG autostart entries (~/.config/autostart shadowing
 * /etc/xdg/autostart) once the session is up. Honors Hidden,
 * OnlyShowIn/NotShowIn against XDG_CURRENT_DESKTOP, TryExec and
 * X-GNOME-Autostart-enabled. No-op in nested --devkit sessions.
 */
void ooze_autostart_run (void);

/*
 * Decide whether an autostart .desktop entry should run in a @desktop
 * session (a name like "Ooze"). Pure function over the [Desktop Entry]
 * group so it can be unit-tested: honors Hidden, X-GNOME-Autostart-enabled,
 * TryExec (PATH lookup) and OnlyShowIn/NotShowIn.
 */
gboolean ooze_autostart_entry_should_run (GKeyFile   *keyfile,
                                          const char *desktop);

G_END_DECLS
