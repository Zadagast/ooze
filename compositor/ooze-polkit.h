#pragma once

#include "ooze-plugin-priv.h"

G_BEGIN_DECLS

/*
 * In-compositor polkit authentication agent. Registers for the login
 * session and pops an Aqua password dialog when an app requests admin
 * authorization (package installs, disk mounts, ...). No-op in nested
 * --devkit sessions (the host session already has an agent).
 */
void ooze_polkit_init (OozePlugin *plugin);
void ooze_polkit_shutdown (void);

G_END_DECLS
