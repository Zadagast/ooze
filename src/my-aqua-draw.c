#include "my-aqua-draw.h"
#include "my-theme.h"

#define __COGL_H_INSIDE__
#include "cogl/cogl-texture-2d.h"
#undef __COGL_H_INSIDE__

#include <cairo/cairo.h>
#include <math.h>
#include <pango/pangocairo.h>

static ClutterContext *
my_aqua_get_context (ClutterActor *ref_actor)
{
  while (ref_actor)
    {
      ClutterContext *context = clutter_actor_get_context (ref_actor);

      if (context)
        return context;

      ref_actor = clutter_actor_get_parent (ref_actor);
    }

  return NULL;
}

static CoglTexture *
my_aqua_surface_to_texture (ClutterActor     *ref_actor,
                            cairo_surface_t  *surface)
{
  ClutterContext *clutter_context;
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  int stride;
  guchar *data;

  if (!surface)
    return NULL;

  clutter_context = my_aqua_get_context (ref_actor);
  if (!clutter_context)
    return NULL;

  backend = clutter_context_get_backend (clutter_context);
  cogl_context = clutter_backend_get_cogl_context (backend);
  if (!cogl_context)
    return NULL;

  cairo_surface_flush (surface);

  width = cairo_image_surface_get_width (surface);
  height = cairo_image_surface_get_height (surface);
  stride = cairo_image_surface_get_stride (surface);
  data = cairo_image_surface_get_data (surface);

  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width,
                                           height,
                                           COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                           stride,
                                           data,
                                           &error);
  if (!texture)
    g_warning ("MyAqua: failed to upload texture: %s",
               error ? error->message : "unknown error");

  return texture;
}

static ClutterContent *
my_aqua_content_from_surface (ClutterActor     *ref_actor,
                              cairo_surface_t  *surface)
{
  g_autoptr (CoglTexture) texture = NULL;

  texture = my_aqua_surface_to_texture (ref_actor, surface);
  if (!texture)
    return NULL;

  return clutter_texture_content_new_from_texture (texture, NULL);
}

static void
my_aqua_rounded_rect (cairo_t *cr,
                      gdouble  x,
                      gdouble  y,
                      gdouble  w,
                      gdouble  h,
                      gdouble  r)
{
  gdouble degrees = G_PI / 180.0;

  if (w < 2 * r)
    r = w / 2.0;
  if (h < 2 * r)
    r = h / 2.0;

  cairo_new_sub_path (cr);
  cairo_arc (cr, x + w - r, y + r, r, -90 * degrees, 0 * degrees);
  cairo_arc (cr, x + w - r, y + h - r, r, 0 * degrees, 90 * degrees);
  cairo_arc (cr, x + r, y + h - r, r, 90 * degrees, 180 * degrees);
  cairo_arc (cr, x + r, y + r, r, 180 * degrees, 270 * degrees);
  cairo_close_path (cr);
}

static void
my_aqua_draw_pinstripe (cairo_t             *cr,
                        int                  width,
                        int                  height,
                        const MyAquaPalette *palette)
{
  int y;

  cairo_set_source_rgb (cr,
                        palette->pinstripe_base_r,
                        palette->pinstripe_base_g,
                        palette->pinstripe_base_b);
  cairo_paint (cr);

  for (y = 0; y < height; y += 2)
    {
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, palette->pinstripe_highlight_a);
      cairo_rectangle (cr, 0, y, width, 1);
      cairo_fill (cr);
    }

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, MIN (palette->pinstripe_highlight_a + 0.3, 1.0));
  cairo_rectangle (cr, 0, 0, width, 1);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, palette->pinstripe_shadow_a);
  cairo_rectangle (cr, 0, height - 1, width, 1);
  cairo_fill (cr);
}

ClutterContent *
my_aqua_pinstripe_content (ClutterActor *ref_actor,
                           int           width,
                           int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  my_aqua_draw_pinstripe (cr, width, height, my_theme_get_palette (NULL));
  cairo_destroy (cr);

  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_wallpaper_content (ClutterActor *ref_actor,
                           int           width,
                           int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_pattern_t *radial;
  cairo_pattern_t *linear;
  ClutterContent *content;
  gdouble cx;
  gdouble cy;
  gdouble radius;
  int y;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  cx = width * 0.52;
  cy = height * 0.38;
  radius = hypot (width, height) * 0.72;

  radial = cairo_pattern_create_radial (cx, cy, 0.0, cx, cy, radius);
  cairo_pattern_add_color_stop_rgb (radial, 0.0,
                                    palette->wallpaper_center_r,
                                    palette->wallpaper_center_g,
                                    palette->wallpaper_center_b);
  cairo_pattern_add_color_stop_rgb (radial, 0.55,
                                    palette->wallpaper_mid_r,
                                    palette->wallpaper_mid_g,
                                    palette->wallpaper_mid_b);
  cairo_pattern_add_color_stop_rgb (radial, 1.0,
                                    palette->wallpaper_edge_r,
                                    palette->wallpaper_edge_g,
                                    palette->wallpaper_edge_b);
  cairo_set_source (cr, radial);
  cairo_paint (cr);
  cairo_pattern_destroy (radial);

  linear = cairo_pattern_create_linear (0, 0, 0, height);
  cairo_pattern_add_color_stop_rgba (linear, 0.0, 1.0, 1.0, 1.0, 0.08);
  cairo_pattern_add_color_stop_rgba (linear, 0.35, 1.0, 1.0, 1.0, 0.0);
  cairo_pattern_add_color_stop_rgba (linear, 1.0, 0.0, 0.0, 0.0, 0.12);
  cairo_set_source (cr, linear);
  cairo_paint (cr);
  cairo_pattern_destroy (linear);

  for (y = 0; y < height; y += 3)
    {
      gdouble alpha = 0.03 + 0.02 * sin (y * 0.08);

      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, alpha);
      cairo_rectangle (cr, 0, y, width, 1);
      cairo_fill (cr);
    }

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_dock_plate_content (ClutterActor *ref_actor,
                            int           width,
                            int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_pattern_t *gradient;
  ClutterContent *content;
  gdouble radius;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);
  gdouble glass_r = my_theme_is_dark (NULL) ? 0.14 : 1.0;
  gdouble glass_g = my_theme_is_dark (NULL) ? 0.14 : 1.0;
  gdouble glass_b = my_theme_is_dark (NULL) ? 0.16 : 1.0;

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  radius = MIN (height / 2.0, 18.0);

  my_aqua_rounded_rect (cr, 0.5, 0.5, width - 1.0, height - 1.0, radius);

  gradient = cairo_pattern_create_linear (0, 0, 0, height);
  cairo_pattern_add_color_stop_rgba (gradient, 0.0,
                                     glass_r, glass_g, glass_b,
                                     palette->dock_top_a);
  cairo_pattern_add_color_stop_rgba (gradient, 0.45,
                                     glass_r, glass_g, glass_b,
                                     palette->dock_mid_a);
  cairo_pattern_add_color_stop_rgba (gradient, 1.0,
                                     glass_r * 0.92, glass_g * 0.94, glass_b * 0.98,
                                     palette->dock_bottom_a);
  cairo_set_source (cr, gradient);
  cairo_fill_preserve (cr);
  cairo_pattern_destroy (gradient);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, palette->dock_border_a);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.12);
  cairo_set_line_width (cr, 1.0);
  my_aqua_rounded_rect (cr, 0.5, 0.5, width - 1.0, height - 1.0, radius);
  cairo_stroke (cr);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.35);
  my_aqua_rounded_rect (cr, 2.0, 2.0, width - 4.0, (height - 4.0) * 0.45, radius - 2.0);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_dock_icon_content (ClutterActor *ref_actor,
                           int           size,
                           gfloat        r,
                           gfloat        g,
                           gfloat        b)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_pattern_t *gloss;
  ClutterContent *content;
  gdouble radius;

  if (size < 1)
    size = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  radius = size * 0.19;

  my_aqua_rounded_rect (cr, 0.5, 0.5, size - 1.0, size - 1.0, radius);
  cairo_set_source_rgb (cr, r, g, b);
  cairo_fill_preserve (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.35);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  gloss = cairo_pattern_create_linear (0, 0, 0, size * 0.55);
  cairo_pattern_add_color_stop_rgba (gloss, 0.0, 1.0, 1.0, 1.0, 0.55);
  cairo_pattern_add_color_stop_rgba (gloss, 1.0, 1.0, 1.0, 1.0, 0.0);
  cairo_set_source (cr, gloss);
  my_aqua_rounded_rect (cr, 1.0, 1.0, size - 2.0, size * 0.52, radius - 1.0);
  cairo_fill (cr);
  cairo_pattern_destroy (gloss);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.18);
  my_aqua_rounded_rect (cr, 2.0, 2.0, size - 4.0, size * 0.22, radius - 2.0);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_traffic_light_content (ClutterActor *ref_actor,
                               int           size,
                               gfloat        r,
                               gfloat        g,
                               gfloat        b)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  gdouble radius;
  gdouble cx;
  gdouble cy;

  if (size < 1)
    size = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  cx = size / 2.0;
  cy = size / 2.0;
  radius = size / 2.0 - 0.75;

  cairo_arc (cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_set_source_rgb (cr, r, g, b);
  cairo_fill_preserve (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.35);
  cairo_set_line_width (cr, 0.75);
  cairo_stroke (cr);

  cairo_arc (cr, cx - radius * 0.28, cy - radius * 0.32, radius * 0.22, 0, 2 * G_PI);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.45);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}


ClutterContent *
my_aqua_apple_logo_content (ClutterActor *ref_actor,
                            int           size)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  gdouble scale;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);

  if (size < 1)
    size = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  scale = size / 16.0;
  cairo_scale (cr, scale, scale);
  cairo_translate (cr, 0.5, 0.5);

  cairo_move_to (cr, 8.0, 1.5);
  cairo_curve_to (cr, 8.8, 0.4, 10.0, 0.0, 11.0, 0.6);
  cairo_curve_to (cr, 10.2, 1.8, 10.6, 3.4, 11.6, 4.2);
  cairo_curve_to (cr, 10.4, 4.3, 9.3, 3.6, 8.0, 3.6);
  cairo_curve_to (cr, 6.7, 3.6, 5.6, 4.3, 4.4, 4.2);
  cairo_curve_to (cr, 5.4, 3.3, 5.9, 1.8, 5.0, 0.6);
  cairo_curve_to (cr, 6.0, 0.0, 7.2, 0.4, 8.0, 1.5);
  cairo_close_path (cr);

  cairo_move_to (cr, 8.0, 4.0);
  cairo_curve_to (cr, 10.6, 4.0, 12.4, 5.8, 12.4, 8.6);
  cairo_curve_to (cr, 12.4, 12.0, 10.0, 15.0, 8.0, 15.0);
  cairo_curve_to (cr, 6.0, 15.0, 3.6, 12.0, 3.6, 8.6);
  cairo_curve_to (cr, 3.6, 5.8, 5.4, 4.0, 8.0, 4.0);
  cairo_close_path (cr);

  cairo_set_source_rgba (cr,
                         palette->menu_text_r,
                         palette->menu_text_g,
                         palette->menu_text_b,
                         1.0);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_text_content (ClutterActor *ref_actor,
                      const char   *font_desc,
                      const char   *text,
                      gfloat        r,
                      gfloat        g,
                      gfloat        b,
                      int          *width_out,
                      int          *height_out)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  PangoLayout *layout;
  PangoFontDescription *font;
  int width;
  int height;
  PangoRectangle ink;
  PangoRectangle logical;

  if (!text || text[0] == '\0')
    text = " ";

  font = pango_font_description_from_string (font_desc ?
                                             font_desc :
                                             "Sans 11");

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
  cr = cairo_create (surface);
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (layout, font);
  pango_layout_set_text (layout, text, -1);
  pango_layout_get_pixel_extents (layout, &ink, &logical);
  g_object_unref (layout);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  width = MAX (logical.width + 4, 1);
  height = MAX (logical.height + 4, 1);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (layout, font);
  pango_layout_set_text (layout, text, -1);

  cairo_set_source_rgb (cr, r, g, b);
  cairo_move_to (cr, 2.0, 2.0);
  pango_cairo_show_layout (cr, layout);

  g_object_unref (layout);
  pango_font_description_free (font);
  cairo_destroy (cr);

  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  if (width_out)
    *width_out = width;
  if (height_out)
    *height_out = height;

  return content;
}

void
my_aqua_actor_set_content (ClutterActor   *actor,
                           ClutterContent *content,
                           int             width,
                           int             height)
{
  if (!content)
    return;

  if (width > 0 && height > 0)
    clutter_actor_set_size (actor, (gfloat) width, (gfloat) height);

  clutter_actor_set_content (actor, content);
  g_object_unref (content);
}

void
my_aqua_actor_set_scaled_content (ClutterActor   *actor,
                                  ClutterContent *content,
                                  int             display_w,
                                  int             display_h,
                                  int             texture_w,
                                  int             texture_h)
{
  if (!content)
    return;

  if (display_w > 0 && display_h > 0)
    clutter_actor_set_size (actor, (gfloat) display_w, (gfloat) display_h);

  if (texture_w > display_w || texture_h > display_h)
    {
      clutter_actor_set_content_scaling_filters (actor,
                                                 CLUTTER_SCALING_FILTER_TRILINEAR,
                                                 CLUTTER_SCALING_FILTER_LINEAR);
    }

  clutter_actor_set_content (actor, content);
  g_object_unref (content);
}

int
my_aqua_icon_texture_size (MetaDisplay *display,
                           int          logical_size)
{
  gfloat scale = 2.0f;

  if (logical_size < 1)
    logical_size = 1;

  if (display)
    {
      scale = meta_display_get_monitor_scale (display, 0);
      if (scale < 1.0f)
        scale = 1.0f;
      scale = MAX (scale, 2.0f);
    }

  return (int) (logical_size * scale + 0.5f);
}
