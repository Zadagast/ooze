#include "my-aqua-draw.h"
#include "my-theme.h"

#include "../common/aqua-chrome.h"
#include "../common/ooze-font.h"

#define __COGL_H_INSIDE__
#include "cogl/cogl-texture-2d.h"
#undef __COGL_H_INSIDE__

#include <cairo/cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
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

ClutterContent *
my_aqua_content_from_surface (ClutterActor    *ref_actor,
                              cairo_surface_t *surface)
{
  g_autoptr (CoglTexture) texture = NULL;

  texture = my_aqua_surface_to_texture (ref_actor, surface);
  if (!texture)
    return NULL;

  return clutter_texture_content_new_from_texture (texture, NULL);
}

static char *
my_aqua_find_data_path (const char *filename)
{
  const char *env_dir;
  g_autofree char *path = NULL;
  static const char *fallback_dirs[] = {
    "data",
    "../data",
    NULL,
  };
  gsize i;

  env_dir = g_getenv ("OOZE_DATA_DIR");
  if (env_dir && env_dir[0] != '\0')
    {
      path = g_build_filename (env_dir, filename, NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        return g_steal_pointer (&path);
    }

#ifdef OOZE_DATA_DIR
  path = g_build_filename (OOZE_DATA_DIR, filename, NULL);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    return g_steal_pointer (&path);
#endif

  for (i = 0; fallback_dirs[i] != NULL; i++)
    {
      path = g_build_filename (fallback_dirs[i], filename, NULL);
      if (g_file_test (path, G_FILE_TEST_EXISTS))
        return g_steal_pointer (&path);
    }

  return NULL;
}

static GdkPixbuf *
my_aqua_load_data_pixbuf (const char *filename,
                          int         width,
                          int         height)
{
  g_autofree char *path = my_aqua_find_data_path (filename);
  g_autoptr (GError) error = NULL;

  if (!path)
    return NULL;

  return gdk_pixbuf_new_from_file_at_scale (path,
                                            width,
                                            height,
                                            TRUE,
                                            &error);
}

static ClutterContent *
my_aqua_content_from_pixbuf (ClutterActor *ref_actor,
                             GdkPixbuf    *pixbuf)
{
  ClutterContext *clutter_context;
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  int rowstride;
  CoglPixelFormat format;

  clutter_context = my_aqua_get_context (ref_actor);
  if (!clutter_context)
    return NULL;

  backend = clutter_context_get_backend (clutter_context);
  cogl_context = clutter_backend_get_cogl_context (backend);
  if (!cogl_context)
    return NULL;

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  format = gdk_pixbuf_get_has_alpha (pixbuf)
    ? COGL_PIXEL_FORMAT_RGBA_8888
    : COGL_PIXEL_FORMAT_RGB_888;

  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width,
                                           height,
                                           format,
                                           rowstride,
                                           gdk_pixbuf_get_pixels (pixbuf),
                                           &error);
  if (!texture)
    {
      g_warning ("MyAqua: failed to upload pixbuf texture: %s",
                 error ? error->message : "unknown error");
      return NULL;
    }

  return clutter_texture_content_new_from_texture (texture, NULL);
}

static void
my_aqua_draw_feather (cairo_t             *cr,
                      gdouble              width,
                      gdouble              height,
                      const MyAquaPalette *palette)
{
  gdouble cy;
  int i;

  cairo_save (cr);
  cairo_scale (cr, width / 32.0, height / 12.0);

  cy = 6.0;

  cairo_move_to (cr, 2.0, cy);
  cairo_curve_to (cr, 8.0, cy - 2.2, 24.0, cy - 2.0, 30.0, cy);
  cairo_curve_to (cr, 24.0, cy + 2.0, 8.0, cy + 2.2, 2.0, cy);
  cairo_close_path (cr);
  cairo_set_source_rgba (cr,
                         palette->menu_text_r,
                         palette->menu_text_g,
                         palette->menu_text_b,
                         0.92);
  cairo_fill (cr);

  cairo_move_to (cr, 3.0, cy);
  cairo_curve_to (cr, 10.0, cy - 0.35, 22.0, cy - 0.35, 29.0, cy);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.22);
  cairo_set_line_width (cr, 0.8);
  cairo_stroke (cr);

  cairo_move_to (cr, 4.0, cy);
  cairo_line_to (cr, 28.0, cy);
  cairo_set_source_rgba (cr,
                         palette->menu_text_r * 0.55,
                         palette->menu_text_g * 0.55,
                         palette->menu_text_b * 0.55,
                         0.85);
  cairo_set_line_width (cr, 0.55);
  cairo_stroke (cr);

  for (i = 0; i < 8; i++)
    {
      gdouble x = 5.0 + i * 3.1;

      cairo_move_to (cr, x, cy - 0.15);
      cairo_line_to (cr, x + 1.6, cy - 1.8);
      cairo_move_to (cr, x + 0.4, cy + 0.15);
      cairo_line_to (cr, x + 2.0, cy + 1.8);
    }
  cairo_set_source_rgba (cr,
                         palette->menu_text_r,
                         palette->menu_text_g,
                         palette->menu_text_b,
                         0.55);
  cairo_set_line_width (cr, 0.45);
  cairo_stroke (cr);

  cairo_restore (cr);
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
my_aqua_dock_reflection_content (ClutterActor   *ref_actor,
                                 ClutterContent *source,
                                 int             reflect_h)
{
  CoglTexture *texture;
  cairo_surface_t *src_surface;
  cairo_surface_t *dst_surface;
  cairo_t *cr;
  cairo_pattern_t *pat;
  cairo_pattern_t *fade;
  cairo_matrix_t matrix;
  ClutterContent *content;
  g_autofree uint8_t *pixels = NULL;
  int width;
  int height;
  int stride;
  int nbytes;

  if (!source || !CLUTTER_IS_TEXTURE_CONTENT (source) || reflect_h < 1)
    return NULL;

  texture = clutter_texture_content_get_texture (CLUTTER_TEXTURE_CONTENT (source));
  if (!texture)
    return NULL;

  width = cogl_texture_get_width (texture);
  height = cogl_texture_get_height (texture);
  if (width < 1 || height < 1)
    return NULL;

  if (reflect_h > height)
    reflect_h = height;

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, width);
  nbytes = stride * height;
  pixels = g_malloc0 ((gsize) nbytes);
  if (cogl_texture_get_data (texture,
                             COGL_PIXEL_FORMAT_BGRA_8888_PRE,
                             (unsigned int) stride,
                             pixels) <= 0)
    return NULL;

  src_surface = cairo_image_surface_create_for_data (pixels,
                                                     CAIRO_FORMAT_ARGB32,
                                                     width,
                                                     height,
                                                     stride);
  dst_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, reflect_h);
  cr = cairo_create (dst_surface);

  /* Flip so dest y=0 samples the icon's bottom edge. */
  pat = cairo_pattern_create_for_surface (src_surface);
  cairo_matrix_init (&matrix, 1.0, 0.0, 0.0, -1.0, 0.0, (double) height);
  cairo_pattern_set_matrix (pat, &matrix);
  cairo_set_source (cr, pat);
  cairo_rectangle (cr, 0.0, 0.0, (double) width, (double) reflect_h);
  cairo_fill (cr);
  cairo_pattern_destroy (pat);

  /* Soft dissolve away from the shelf. */
  cairo_set_operator (cr, CAIRO_OPERATOR_DEST_IN);
  fade = cairo_pattern_create_linear (0.0, 0.0, 0.0, (double) reflect_h);
  cairo_pattern_add_color_stop_rgba (fade, 0.0, 0.0, 0.0, 0.0, 0.42);
  cairo_pattern_add_color_stop_rgba (fade, 0.45, 0.0, 0.0, 0.0, 0.18);
  cairo_pattern_add_color_stop_rgba (fade, 1.0, 0.0, 0.0, 0.0, 0.0);
  cairo_set_source (cr, fade);
  cairo_paint (cr);
  cairo_pattern_destroy (fade);

  cairo_destroy (cr);
  cairo_surface_destroy (src_surface);

  content = my_aqua_content_from_surface (ref_actor, dst_surface);
  cairo_surface_destroy (dst_surface);

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

  if (size < 1)
    size = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  aqua_traffic_draw_circle (cr,
                            size / 2.0,
                            size / 2.0,
                            size,
                            r,
                            g,
                            b);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

#define OOZE_BUTTON_EMOJI "🫟"
#define OOZE_BUTTON_TEXT  " Ooze"
#define OOZE_BUTTON_LABEL OOZE_BUTTON_EMOJI OOZE_BUTTON_TEXT

ClutterContent *
my_aqua_ooze_button_content (ClutterActor *ref_actor,
                             int          *width_out,
                             int          *height_out)
{
  cairo_surface_t *surface;
  cairo_surface_t *emoji_surf;
  cairo_t *cr;
  cairo_t *emoji_cr;
  ClutterContent *content;
  PangoLayout *layout;
  PangoLayout *emoji_layout;
  PangoLayout *text_layout;
  PangoFontDescription *font;
  PangoRectangle ink;
  PangoRectangle logical;
  PangoRectangle emoji_ink;
  PangoRectangle emoji_logical;
  PangoRectangle text_ink;
  PangoRectangle text_logical;
  cairo_pattern_t *gradient;
  gdouble radius;
  gdouble pad_x = 12.0;
  gdouble height = 22.0;  /* PANEL_HEIGHT 26 − 2×2 pad */
  gdouble width;
  gdouble text_x;
  gdouble word_x;
  gdouble word_y;
  gdouble emoji_x;
  gdouble emoji_y;
  int emoji_pad = 2;
  int emoji_sw;
  int emoji_sh;
  /* 1 px cardinal offsets — thin, sharp silhouette */
  static const gdouble outline_dx[] = { -1.0, 0.0, 0.0, 1.0 };
  static const gdouble outline_dy[] = {  0.0,-1.0, 1.0, 0.0 };
  guint i;

  font = pango_font_description_from_string (OOZE_UI_FONT_EMPHASIS);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
  cr = cairo_create (surface);
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (layout, font);
  pango_layout_set_text (layout, OOZE_BUTTON_LABEL, -1);
  pango_layout_get_pixel_extents (layout, &ink, &logical);
  g_object_unref (layout);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  width = logical.width + pad_x * 2.0;
  if (width < 80.0)
    width = 80.0;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        (int) ceil (width),
                                        (int) ceil (height));
  cr = cairo_create (surface);

  radius = 5.0;  /* squircle corner — rounded but not a pill */
  my_aqua_rounded_rect (cr, 0.5, 0.5, width - 1.0, height - 1.0, radius);

  /* XP-style gradient: slightly darker lime top so white text reads cleanly */
  gradient = cairo_pattern_create_linear (0, 0, 0, height);
  cairo_pattern_add_color_stop_rgb (gradient, 0.00, 0.42, 0.78, 0.22);
  cairo_pattern_add_color_stop_rgb (gradient, 0.40, 0.22, 0.68, 0.12);
  cairo_pattern_add_color_stop_rgb (gradient, 0.70, 0.10, 0.50, 0.08);
  cairo_pattern_add_color_stop_rgb (gradient, 1.00, 0.05, 0.32, 0.04);
  cairo_set_source (cr, gradient);
  cairo_fill_preserve (cr);
  cairo_pattern_destroy (gradient);

  /* Outer border */
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.40);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* Top gloss rim — only top 28% so it doesn't wash over the label */
  my_aqua_rounded_rect (cr, 1.5, 1.5, width - 3.0, (height - 3.0) * 0.28, radius - 1.0);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.42);
  cairo_fill (cr);

  /* Measure pieces separately so each can be optically centered */
  emoji_layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (emoji_layout, font);
  pango_layout_set_text (emoji_layout, OOZE_BUTTON_EMOJI, -1);
  pango_layout_get_pixel_extents (emoji_layout, &emoji_ink, &emoji_logical);

  text_layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (text_layout, font);
  pango_layout_set_text (text_layout, OOZE_BUTTON_TEXT, -1);
  pango_layout_get_pixel_extents (text_layout, &text_ink, &text_logical);

  text_x = (width - (emoji_logical.width + text_logical.width)) / 2.0;
  word_x = text_x + emoji_logical.width;
  /* Center each piece on the button midline using its own ink box */
  word_y = (height - text_ink.height) / 2.0 - text_ink.y;
  emoji_sw = emoji_logical.width + emoji_pad * 2;
  emoji_sh = MAX (emoji_logical.height, emoji_ink.height) + emoji_pad * 2;
  if (emoji_sw < 1) emoji_sw = 1;
  if (emoji_sh < 1) emoji_sh = 1;

  emoji_surf = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, emoji_sw, emoji_sh);
  emoji_cr = cairo_create (emoji_surf);
  cairo_move_to (emoji_cr,
                 emoji_pad - emoji_ink.x,
                 emoji_pad - emoji_ink.y);
  pango_cairo_show_layout (emoji_cr, emoji_layout);
  cairo_destroy (emoji_cr);
  g_object_unref (emoji_layout);

  /* Ink sits at (emoji_pad, emoji_pad) in the surface — center that ink box */
  emoji_x = text_x - emoji_pad + emoji_ink.x;
  emoji_y = (height - emoji_ink.height) / 2.0 - emoji_pad;

  /* Thin black silhouette following the splat shape (mask uses glyph alpha) */
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
  for (i = 0; i < G_N_ELEMENTS (outline_dx); i++)
    cairo_mask_surface (cr, emoji_surf,
                        emoji_x + outline_dx[i],
                        emoji_y + outline_dy[i]);

  /* Color emoji on top */
  cairo_set_source_surface (cr, emoji_surf, emoji_x, emoji_y);
  cairo_paint (cr);
  cairo_surface_destroy (emoji_surf);

  /* " Ooze" word — white with drop shadow */
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.55);
  cairo_move_to (cr, word_x + 1.0, word_y + 1.0);
  pango_cairo_show_layout (cr, text_layout);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.30);
  cairo_move_to (cr, word_x + 0.5, word_y + 0.5);
  pango_cairo_show_layout (cr, text_layout);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_move_to (cr, word_x, word_y);
  pango_cairo_show_layout (cr, text_layout);

  g_object_unref (text_layout);
  pango_font_description_free (font);
  cairo_destroy (cr);

  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  if (width_out)
    *width_out = (int) ceil (width);
  if (height_out)
    *height_out = (int) ceil (height);

  return content;
}

ClutterContent *
my_aqua_menu_feather_content (ClutterActor *ref_actor,
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
  my_aqua_draw_feather (cr, width, height, my_theme_get_palette (NULL));
  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

ClutterContent *
my_aqua_spot_icon_content (ClutterActor *ref_actor,
                           MetaDisplay  *display,
                           int           logical_size)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  int texture;

  if (logical_size < 1)
    logical_size = 1;

  texture = my_aqua_icon_texture_size (display, logical_size);
  pixbuf = my_aqua_load_data_pixbuf ("spot-logo.svg", texture, texture);
  if (!pixbuf)
    {
      g_warning ("MyAqua: spot-logo.svg not found; using fallback tile");
      return my_aqua_dock_icon_content (ref_actor,
                                        texture,
                                        0.22f,
                                        0.48f,
                                        0.92f);
    }

  return my_aqua_content_from_pixbuf (ref_actor, pixbuf);
}

ClutterContent *
my_aqua_apple_logo_content (ClutterActor *ref_actor,
                            int           size)
{
  return my_aqua_menu_feather_content (ref_actor, size, size);
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
  else if (!g_utf8_validate (text, -1, NULL))
    text = "�";

  font = pango_font_description_from_string (font_desc ?
                                             font_desc :
                                             OOZE_UI_FONT);

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
