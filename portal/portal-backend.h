#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

void ooze_portal_backend_start (GDBusConnection *connection);
void ooze_portal_backend_ensure_gtk (void);

G_END_DECLS
