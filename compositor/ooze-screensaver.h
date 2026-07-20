#pragma once

#include "ooze-plugin.h"

#include <glib.h>

G_BEGIN_DECLS

void     ooze_screensaver_init      (OozePlugin *plugin);
void     ooze_screensaver_dispose   (OozePlugin *plugin);
void     ooze_screensaver_activate  (OozePlugin *plugin);
void     ooze_screensaver_dismiss   (OozePlugin *plugin);
void     ooze_screensaver_rearm     (OozePlugin *plugin);
void     ooze_screensaver_lock_backdrop (OozePlugin *plugin);
void     ooze_screensaver_unlock_backdrop (OozePlugin *plugin);
gboolean ooze_screensaver_is_active (OozePlugin *plugin);

G_END_DECLS
