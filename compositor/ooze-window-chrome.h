#pragma once

#include <meta/meta-plugin.h>
#include <meta/meta-window-actor.h>
#include <meta/window.h>
#include <glib.h>

G_BEGIN_DECLS

/*
 * Optional Clutter SSD overlay (pinstripe + traffic lights).
 *
 * Foreign apps normally do not use this: X11/XWayland get Mutter MetaFrames,
 * and Wayland-native apps keep CSD with left-side controls via button-layout.
 * Kept for possible fallback / experiments; state lives on MetaWindowActor.
 */

/* Attach a compositor-level titlebar + traffic lights to a foreign window. */
void ooze_window_chrome_apply          (MetaWindowActor *actor,
                                        MetaPlugin      *plugin);

/* Re-position/repaint the chrome to match the window's current frame rect. */
void ooze_window_chrome_sync           (MetaWindowActor *actor);

/* Schedule a chrome sync on the next idle cycle (safe to call repeatedly). */
void ooze_window_chrome_schedule_sync  (MetaWindowActor *actor);

/* Cancel any pending scheduled sync without performing it. */
void ooze_window_chrome_cancel_sync    (MetaWindowActor *actor);

/* Destroy and detach the chrome, then free all associated resources. */
void ooze_window_chrome_remove         (MetaWindowActor *actor);

/* Show or hide the chrome without destroying it (used during animations). */
void ooze_window_chrome_set_visible    (MetaWindowActor *actor,
                                        gboolean         visible);

/* Raise the SSD titlebar actor above its sibling window actor. */
void ooze_window_chrome_raise_ssd      (MetaWindowActor *actor);

/*
 * Invalidate cached sizes/text and schedule a redraw.  Call after a theme
 * change so the titlebar is repainted with the new palette.
 */
void ooze_window_chrome_invalidate     (MetaWindowActor *actor);

G_END_DECLS
