#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Publish WAYLAND_DISPLAY / DISPLAY to the systemd --user and D-Bus
 * activation environments once the compositor owns them, then kick the
 * portal services so they start against a live display. No-op in nested
 * --devkit sessions (never pollute the host session).
 */
void ooze_portal_env_publish (void);

G_END_DECLS
