#pragma once

#include <meta/meta-plugin.h>
#include <meta/meta-window-actor.h>

G_BEGIN_DECLS

typedef void (*MyMagicLampDoneFunc) (MetaPlugin      *plugin,
                                     MetaWindowActor *actor,
                                     gboolean         unminimize,
                                     gpointer         user_data);

/* Compiz/macOS-style mesh warp into the dock icon. */
void my_magic_lamp_run (MetaPlugin         *plugin,
                        MetaWindowActor    *actor,
                        gboolean            unminimize,
                        gfloat              icon_x,
                        gfloat              icon_y,
                        gfloat              icon_w,
                        gfloat              icon_h,
                        MyMagicLampDoneFunc done,
                        gpointer            user_data);

void my_magic_lamp_cancel (MetaWindowActor *actor);

G_END_DECLS
