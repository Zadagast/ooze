#pragma once

#include "ooze-plugin.h"

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _OozeShot OozeShot;

OozeShot *ooze_shot_new (OozePlugin *plugin);
gboolean  ooze_shot_capture_desktop (OozeShot *shot,
                                     GError  **error);
void      ooze_shot_free (OozeShot *shot);

G_END_DECLS
