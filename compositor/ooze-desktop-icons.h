#pragma once

#include <clutter/clutter.h>
#include <meta/display.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

ClutterActor *ooze_desktop_icons_create (MetaContext  *context,
                                       MetaDisplay  *display,
                                       ClutterActor *ref_actor,
                                       int           monitor,
                                       int           width,
                                       int           height);

void ooze_desktop_icons_begin_shutdown (ClutterActor *container);

G_END_DECLS
