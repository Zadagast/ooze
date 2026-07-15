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

G_END_DECLS
