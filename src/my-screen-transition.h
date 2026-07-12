#pragma once

#include <clutter/clutter.h>
#include <meta/meta-plugin.h>

G_BEGIN_DECLS

/*
 * GNOME/elementary-style appearance transition:
 * snapshot the stage, swap light/dark underneath, fade the snapshot out.
 */
void my_screen_transition_run (MetaPlugin *plugin);

G_END_DECLS
