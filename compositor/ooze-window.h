#pragma once

#include <meta/display.h>
#include <meta/meta-window-actor.h>

gboolean ooze_window_is_client_decorated (MetaWindow *window);
gboolean ooze_window_uses_ooze_client_chrome (MetaWindow *window);

/* No-ops: move/resize/edge-tile belong to Mutter (MetaFrames / client CSD). */
void ooze_window_setup (MetaWindowActor *actor);
void ooze_window_sync (MetaWindowActor *actor);
void ooze_window_schedule_sync (MetaWindowActor *actor);
void ooze_window_cancel_scheduled_sync (MetaWindowActor *actor);
void ooze_window_teardown (MetaWindowActor *actor);
