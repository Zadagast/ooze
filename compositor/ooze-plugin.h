#pragma once

#include <meta/meta-plugin.h>

G_BEGIN_DECLS

#define OOZE_TYPE_PLUGIN (ooze_plugin_get_type ())

G_DECLARE_FINAL_TYPE (OozePlugin, ooze_plugin, OOZE, PLUGIN, MetaPlugin)

GType ooze_plugin_get_type (void);

G_END_DECLS
