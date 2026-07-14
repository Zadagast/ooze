#pragma once

#include <cairo/cairo.h>
#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <meta/display.h>

G_BEGIN_DECLS

ClutterContent *ooze_aqua_content_from_surface (ClutterActor    *ref_actor,
                                              cairo_surface_t *surface);

ClutterContent *ooze_aqua_content_from_pixbuf (ClutterActor *ref_actor,
                                             GdkPixbuf    *pixbuf);

ClutterContent *ooze_aqua_pinstripe_content (ClutterActor *ref_actor,
                                           int           width,
                                           int           height);

ClutterContent *ooze_aqua_wallpaper_content (ClutterActor *ref_actor,
                                           int           width,
                                           int           height);

ClutterContent *ooze_aqua_dock_plate_content (ClutterActor *ref_actor,
                                            int           width,
                                            int           height);

ClutterContent *ooze_aqua_dock_icon_content (ClutterActor *ref_actor,
                                           int           size,
                                           gfloat        r,
                                           gfloat        g,
                                           gfloat        b);

/*
 * Build a vertically flipped, bottom-faded reflection from an icon's content.
 * @reflect_h is the output height in pixels (typically ~0.55 × icon size).
 * Returns NULL if @source cannot be sampled.
 */
ClutterContent *ooze_aqua_dock_reflection_content (ClutterActor   *ref_actor,
                                                 ClutterContent *source,
                                                 int             reflect_h);

ClutterContent *ooze_aqua_traffic_light_content (ClutterActor *ref_actor,
                                               int           size,
                                               gfloat        r,
                                               gfloat        g,
                                               gfloat        b);


ClutterContent *ooze_aqua_menu_feather_content (ClutterActor *ref_actor,
                                             int           width,
                                             int           height);

ClutterContent *ooze_aqua_ooze_button_content (ClutterActor *ref_actor,
                                             int          *width_out,
                                             int          *height_out);

/* Squircle glass card (inset=FALSE) or recessed password field (inset=TRUE). */
ClutterContent *ooze_aqua_squircle_panel_content (ClutterActor *ref_actor,
                                                int           width,
                                                int           height,
                                                gboolean      inset);

/* OozeKit primary push — rounded-square lime plate with @label. */
ClutterContent *ooze_aqua_kit_button_content (ClutterActor *ref_actor,
                                            const char   *label,
                                            int           width,
                                            int           height);

ClutterContent *ooze_aqua_spot_icon_content (ClutterActor *ref_actor,
                                           MetaDisplay  *display,
                                           int           logical_size);

ClutterContent *ooze_aqua_apple_logo_content (ClutterActor *ref_actor,
                                            int           size);

ClutterContent *ooze_aqua_text_content (ClutterActor *ref_actor,
                                      const char   *font_desc,
                                      const char   *text,
                                      gfloat        r,
                                      gfloat        g,
                                      gfloat        b,
                                      int          *width_out,
                                      int          *height_out);

/* Like ooze_aqua_text_content, but ellipsize to @max_width_px when > 0. */
ClutterContent *ooze_aqua_text_content_ellipsize (ClutterActor *ref_actor,
                                                const char   *font_desc,
                                                const char   *text,
                                                gfloat        r,
                                                gfloat        g,
                                                gfloat        b,
                                                int           max_width_px,
                                                int          *width_out,
                                                int          *height_out);

void ooze_aqua_actor_set_content (ClutterActor    *actor,
                                ClutterContent  *content,
                                int              width,
                                int              height);

/* Sets @content (sized @texture_w × @texture_h) on an actor displayed at
 * @display_w × @display_h with high-quality downscaling when needed.        */
void ooze_aqua_actor_set_scaled_content (ClutterActor   *actor,
                                       ClutterContent *content,
                                       int             display_w,
                                       int             display_h,
                                       int             texture_w,
                                       int             texture_h);

/* Pixel size to rasterize/load icons at for sharp display on HiDPI.       */
int ooze_aqua_icon_texture_size (MetaDisplay *display,
                               int          logical_size);

G_END_DECLS
