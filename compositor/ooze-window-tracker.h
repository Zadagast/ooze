/* compositor-internal only — not for GTK apps */
#pragma once

#include <meta/display.h>

G_BEGIN_DECLS

typedef struct _OozeWindowTracker OozeWindowTracker;

typedef void (*OozeWindowTrackerChangedFn) (gpointer user_data);

OozeWindowTracker *ooze_window_tracker_new (MetaDisplay *display);

void ooze_window_tracker_free (OozeWindowTracker *tracker);

void ooze_window_tracker_set_changed_callback (OozeWindowTracker         *tracker,
                                               OozeWindowTrackerChangedFn callback,
                                               gpointer                   user_data);

G_END_DECLS
