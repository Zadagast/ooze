#pragma once

#include <clutter/clutter.h>
#include <meta/meta-plugin.h>

G_BEGIN_DECLS

/*
 * Appearance transition: opaque color overlay, swap light/dark underneath,
 * fade out. Intentionally avoids clutter_stage_paint_to_content() — a full
 * stage snapshot stalls the main thread and breaks dock/desktop launches.
 */
void ooze_screen_transition_run (MetaPlugin *plugin);

G_END_DECLS
