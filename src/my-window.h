#pragma once

#include <meta/meta-window-actor.h>

gboolean my_window_is_client_decorated (MetaWindow *window);
gboolean my_window_uses_ooze_client_chrome (MetaWindow *window);

gboolean my_window_begin_grab_from_event (MetaWindow   *window,
                                          ClutterEvent *event,
                                          MetaGrabOp     op);

void my_window_setup (MetaWindowActor *actor);
void my_window_sync (MetaWindowActor *actor);
void my_window_schedule_sync (MetaWindowActor *actor);
void my_window_cancel_scheduled_sync (MetaWindowActor *actor);
void my_window_teardown (MetaWindowActor *actor);
