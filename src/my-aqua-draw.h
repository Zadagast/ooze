#pragma once

#include <clutter/clutter.h>
#include <meta/display.h>

G_BEGIN_DECLS

ClutterContent *my_aqua_pinstripe_content (ClutterActor *ref_actor,
                                           int           width,
                                           int           height);

ClutterContent *my_aqua_wallpaper_content (ClutterActor *ref_actor,
                                           int           width,
                                           int           height);

ClutterContent *my_aqua_dock_plate_content (ClutterActor *ref_actor,
                                            int           width,
                                            int           height);

ClutterContent *my_aqua_dock_icon_content (ClutterActor *ref_actor,
                                           int           size,
                                           gfloat        r,
                                           gfloat        g,
                                           gfloat        b);

ClutterContent *my_aqua_traffic_light_content (ClutterActor *ref_actor,
                                               int           size,
                                               gfloat        r,
                                               gfloat        g,
                                               gfloat        b);


ClutterContent *my_aqua_apple_logo_content (ClutterActor *ref_actor,
                                            int           size);

ClutterContent *my_aqua_text_content (ClutterActor *ref_actor,
                                      const char   *font_desc,
                                      const char   *text,
                                      gfloat        r,
                                      gfloat        g,
                                      gfloat        b,
                                      int          *width_out,
                                      int          *height_out);

void my_aqua_actor_set_content (ClutterActor    *actor,
                                ClutterContent  *content,
                                int              width,
                                int              height);

/* Sets @content (sized @texture_w × @texture_h) on an actor displayed at
 * @display_w × @display_h with high-quality downscaling when needed.        */
void my_aqua_actor_set_scaled_content (ClutterActor   *actor,
                                       ClutterContent *content,
                                       int             display_w,
                                       int             display_h,
                                       int             texture_w,
                                       int             texture_h);

/* Pixel size to rasterize/load icons at for sharp display on HiDPI.       */
int my_aqua_icon_texture_size (MetaDisplay *display,
                               int          logical_size);

G_END_DECLS
