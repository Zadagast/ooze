#pragma once

#include <clutter/clutter.h>
#include <meta/display.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

ClutterActor *my_desktop_icons_create (MetaContext  *context,
                                       MetaDisplay  *display,
                                       ClutterActor *ref_actor,
                                       int           monitor,
                                       int           width,
                                       int           height);

G_END_DECLS
