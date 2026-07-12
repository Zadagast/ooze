#pragma once

#include <meta/meta-plugin.h>

G_BEGIN_DECLS

#define MY_TYPE_PLUGIN (my_plugin_get_type ())

G_DECLARE_FINAL_TYPE (MyPlugin, my_plugin, MY, PLUGIN, MetaPlugin)

GType my_plugin_get_type (void);

G_END_DECLS
