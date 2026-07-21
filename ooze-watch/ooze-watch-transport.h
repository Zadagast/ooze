#pragma once

#include <gtk/gtk.h>

/*
 * OozeWatchTransport — the classic QuickTime-style control deck.
 *
 * One Cairo-drawn widget: brushed-metal bar with an LCD timecode, a scrub
 * channel, a thumbwheel volume slider, and round gel transport buttons that
 * grow toward the central Play control.
 *
 * Signals:
 *   ::play-toggled ()
 *   ::seek-frac    (double frac)   — scrubber released/dragged
 *   ::volume       (double vol01)
 *   ::step         (int direction) — -1 frame back, +1 frame forward
 *   ::jump         (int direction) — -1 rewind 10s, +1 forward 10s
 */
#define OOZE_WATCH_TYPE_TRANSPORT (ooze_watch_transport_get_type ())
G_DECLARE_FINAL_TYPE (OozeWatchTransport, ooze_watch_transport, OOZE_WATCH,
                      TRANSPORT, GtkWidget)

GtkWidget *ooze_watch_transport_new (void);

void ooze_watch_transport_set_state (OozeWatchTransport *self,
                                     double              time_pos,
                                     double              duration,
                                     gboolean            paused,
                                     double              volume01,
                                     gboolean            has_media);
