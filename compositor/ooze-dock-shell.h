#pragma once

#include <clutter/clutter.h>
#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

typedef struct _OozeDock OozeDock;

OozeDock *ooze_dock_new (MetaContext     *context,
                     MetaDisplay     *display,
                     MetaCompositor  *compositor);

void ooze_dock_resize (OozeDock      *dock,
                     MetaDisplay  *display);

ClutterActor *ooze_dock_get_actor (OozeDock *dock);

void ooze_dock_populate_container (MetaContext    *context,
                                 MetaDisplay    *display,
                                 ClutterActor   *stage,
                                 ClutterActor   *container);

ClutterActor *ooze_dock_create_spot_launcher (ClutterActor *stage,
                                            MetaDisplay  *display);

void ooze_dock_launch_spot (MetaContext *context);

void ooze_dock_launch_spot_path (MetaContext *context,
                               const char *path);

void ooze_dock_launch_pak (MetaContext *context);

void ooze_dock_launch_about (MetaContext *context);

/* Push dock-icon rectangles into MetaWindow icon geometry (minimize target). */
void ooze_dock_update_icon_geometries (MetaDisplay  *display,
                                     ClutterActor *icons_container);

ClutterContent *ooze_dock_themed_icon_content (ClutterActor       *ref_actor,
                                             MetaDisplay        *display,
                                             const char * const *icon_names,
                                             int                 logical_size);

void ooze_dock_free (OozeDock *dock);

G_END_DECLS
