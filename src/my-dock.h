#pragma once

#include <clutter/clutter.h>
#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

typedef struct _MyDock MyDock;

MyDock *my_dock_new (MetaContext     *context,
                     MetaDisplay     *display,
                     MetaCompositor  *compositor);

void my_dock_resize (MyDock      *dock,
                     MetaDisplay  *display);

ClutterActor *my_dock_get_actor (MyDock *dock);

void my_dock_populate_container (MetaContext    *context,
                                 MetaDisplay    *display,
                                 ClutterActor   *stage,
                                 ClutterActor   *container);

ClutterActor *my_dock_create_spot_launcher (ClutterActor *stage,
                                            MetaDisplay  *display);

void my_dock_launch_spot (MetaContext *context);

void my_dock_launch_spot_path (MetaContext *context,
                               const char *path);

void my_dock_launch_pak (MetaContext *context);

void my_dock_launch_about (MetaContext *context);

/* Push dock-icon rectangles into MetaWindow icon geometry (minimize target). */
void my_dock_update_icon_geometries (MetaDisplay  *display,
                                     ClutterActor *icons_container);

ClutterContent *my_dock_themed_icon_content (ClutterActor       *ref_actor,
                                             MetaDisplay        *display,
                                             const char * const *icon_names,
                                             int                 logical_size);

void my_dock_free (MyDock *dock);

G_END_DECLS
