#pragma once

#include <meta/meta-window-actor.h>

gboolean ooze_window_is_client_decorated (MetaWindow *window);
gboolean ooze_window_uses_ooze_client_chrome (MetaWindow *window);

gboolean ooze_window_begin_grab_from_event (MetaWindow   *window,
                                          ClutterEvent *event,
                                          MetaGrabOp     op);

void ooze_window_setup (MetaWindowActor *actor);
void ooze_window_sync (MetaWindowActor *actor);
void ooze_window_schedule_sync (MetaWindowActor *actor);
void ooze_window_cancel_scheduled_sync (MetaWindowActor *actor);
void ooze_window_teardown (MetaWindowActor *actor);
