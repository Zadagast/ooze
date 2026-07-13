#pragma once

#include "ooze-plugin.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <glib.h>

G_BEGIN_DECLS

/* Set up the panel actor and all children for the first time. */
void ooze_panel_setup              (OozePlugin       *plugin,
                                    MetaDisplay    *display,
                                    MetaCompositor *compositor);

/* Update the clock label text immediately. */
void ooze_panel_update_clock       (OozePlugin *plugin);

/* Rebuild the menu bar labels from the current app/shell menu. */
void ooze_panel_rebuild_menu_bar   (OozePlugin *plugin);

/* Recolor existing menu bar + clock labels for a Light↔Dark swap (no rebuild). */
void ooze_panel_recolor_menu_bar   (OozePlugin *plugin);

/* Re-lay out menu bar label positions after a resize. */
void ooze_panel_layout_labels      (OozePlugin *plugin);

/* Repaint the Ooze button content (called after a theme change). */
void ooze_panel_refresh_ooze_button (OozePlugin *plugin);

/* Redraw the panel pinstripe texture at the given width. */
void ooze_panel_refresh_texture    (OozePlugin *plugin, int width);

/* Queue a deferred menu bar rebuild (skips if a rebuild is already pending). */
void ooze_panel_schedule_rebuild   (OozePlugin *plugin);

/*
 * Global-menu changed callback — pass directly to
 * ooze_global_menu_set_changed_callback().
 */
void ooze_panel_on_global_menu_changed (gpointer user_data);

/* Cancel all panel idle sources and clear panel actors — call from dispose. */
void ooze_panel_dispose            (OozePlugin *plugin);

G_END_DECLS
