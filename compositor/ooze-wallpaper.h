#pragma once

#include "ooze-plugin.h"

#include <clutter/clutter.h>
#include <glib.h>

G_BEGIN_DECLS

ClutterContent *ooze_wallpaper_content (OozePlugin  *plugin,
                                         ClutterActor *ref_actor,
                                         int           width,
                                         int           height);

void ooze_wallpaper_refresh (OozePlugin *plugin);
void ooze_wallpaper_init    (OozePlugin *plugin);
void ooze_wallpaper_dispose (OozePlugin *plugin);

G_END_DECLS
