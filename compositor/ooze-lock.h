#pragma once

#include "ooze-plugin.h"

#include <glib.h>

G_BEGIN_DECLS

/* Compositor-side lock screen: fullscreen Clutter overlay + input grab.
 * PAM runs in ooze-pam-helper, never on the Mutter UI thread. */

void     ooze_lock_init     (OozePlugin *plugin);
void     ooze_lock_dispose  (OozePlugin *plugin);
void     ooze_lock_request  (OozePlugin *plugin);
gboolean ooze_lock_is_locked (OozePlugin *plugin);

G_END_DECLS
