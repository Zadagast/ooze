#pragma once

#include "ooze-plugin.h"

G_BEGIN_DECLS

typedef struct _OozeNotifications OozeNotifications;

OozeNotifications *ooze_notifications_new (OozePlugin *plugin);
void               ooze_notifications_reflow (OozeNotifications *notifications);
void               ooze_notifications_free (OozeNotifications *notifications);

G_END_DECLS
