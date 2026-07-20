#include "ooze-scenery-window.h"

#include "ooze-about.h"
#include "ooze-action-bar.h"
#include "ooze-button.h"
#include "ooze-flow.h"
#include "ooze-shared-appmenu.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "../ooze-kit/ooze-theme.h"
#include "ooze-toolbar.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <string.h>

typedef enum
{
  SCENERY_PAGE_WALLPAPER,
  SCENERY_PAGE_SCREENSAVER,
} SceneryPage;

typedef struct
{
  GtkWidget *button;
  char *uri;
  GdkPixbuf *pixbuf;
} WallpaperTile;

struct _OozeSceneryWindow
{
  OozeApplicationWindow parent_instance;
  GSettings *background_settings;
  GSettings *scenery_settings;
  GSettings *session_settings;
  GSettings *screensaver_settings;
  GtkWidget *stack;
  GtkWidget *wallpaper_preview;
  GtkWidget *screensaver_preview;
  GtkWidget *wallpaper_tiles;
  GtkWidget *ubuntu_tiles;
  GtkWidget *screensaver_mode_tiles;
  GtkWidget *hack_dropdown;
  GtkStringList *hack_names;
  GtkWidget *start_after;
  GtkWidget *lock_enabled;
  GtkWidget *lock_after;
  GtkWidget *nav_wallpaper;
  GtkWidget *nav_screensaver;
  GtkWidget *custom_tiles;
  GtkWidget *action_bar;
  GdkPixbuf *selected_pixbuf;
  /* Staged (uncommitted) edits — written to GSettings only on Apply. */
  char *pending_wallpaper;   /* "aqua", "aqua-dark", or an absolute path */
  char *pending_mode;        /* "flow", "none" or "xscreensaver:<hack>" */
  guint pending_idle;
  guint pending_lock_delay;
  gboolean pending_lock_enabled;
  gboolean dirty;
  gboolean loading;
  cairo_surface_t *flow_surface;
  int flow_width;
  int flow_height;
  gint64 flow_last_frame_us;
  gdouble flow_phase;
  guint flow_tick_id;
  SceneryPage page;
};

G_DEFINE_FINAL_TYPE (OozeSceneryWindow, ooze_scenery_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void scenery_refresh_selection (OozeSceneryWindow *self);
static void scenery_refresh_preview (OozeSceneryWindow *self);
static void scenery_refresh_mode_selection (OozeSceneryWindow *self);
static void on_choose_clicked (GtkButton *button,
                               gpointer   user_data);

static void
scenery_set_dirty (OozeSceneryWindow *self,
                   gboolean           dirty)
{
  self->dirty = dirty;
  if (self->action_bar)
    ooze_action_bar_set_dirty (self->action_bar, dirty);
}

static void
scenery_mark_dirty (OozeSceneryWindow *self)
{
  if (!self->loading)
    scenery_set_dirty (self, TRUE);
}

static void
scenery_ensure_css (void)
{
  static gboolean loaded;
  GdkDisplay *display;
  GtkCssProvider *provider;

  if (loaded)
    return;
  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    ".scenery-window paned,"
    ".scenery-window paned > *,"
    ".scenery-window stack,"
    ".scenery-window scrolledwindow,"
    ".scenery-window scrolledwindow > viewport,"
    ".scenery-window .ooze-surface {"
    "  border-radius: 0;"
    "  border: none;"
    "  box-shadow: none;"
    "  margin: 0;"
    "  outline: none;"
    "}"
    ".scenery-page { padding: 14px 18px 18px; }"
    ".scenery-preview {"
    "  min-height: 150px;"
    "  border-radius: 8px;"
    "  box-shadow: inset 0 0 0 1px rgba(0,0,0,0.18);"
    "}"
    ".scenery-section {"
    "  margin: 12px 2px 2px;"
    "  font-weight: 700;"
    "  font-size: 0.85em;"
    "  color: alpha(currentColor, 0.55);"
    "}"
    ".scenery-grid { padding: 2px 0; }"
    ".scenery-grid > flowboxchild {"
    "  min-width: 148px;"
    "  border-radius: 5px;"
    "  outline: none;"
    "  background: none;"
    "}"
    ".scenery-grid > flowboxchild:checked {"
    "  background: @accent_bg_color;"
    "}"
    ".scenery-grid > flowboxchild:hover:not(:checked) {"
    "  background: alpha(@accent_bg_color, 0.10);"
    "}"
    ".spot-grid-cell {"
    "  min-width: 140px;"
    "  min-height: 132px;"
    "  padding: 3px 3px 4px;"
    "}"
    ".spot-grid-label {"
    "  color: @window_fg_color;"
    "}"
    ".scenery-grid > flowboxchild:checked .spot-grid-label {"
    "  color: @accent_fg_color;"
    "}"
    ".scenery-tile {"
    "  min-width: 148px;"
    "  min-height: 140px;"
    "  padding: 0;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    ".scenery-tile drawingarea {"
    "  min-width: 128px;"
    "  min-height: 82px;"
    "  border-radius: 5px;"
    "}"
    ".scenery-action-tile {"
    "  min-width: 148px;"
    "  min-height: 140px;"
    "  padding: 0;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    ".scenery-mode-tile {"
    "  min-width: 148px;"
    "  min-height: 140px;"
    "  padding: 0;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    ".scenery-settings-card {"
    "  padding: 12px 14px;"
    "  background: alpha(@card_bg_color, 0.72);"
    "  border-radius: 8px;"
    "  box-shadow: inset 0 0 0 1px rgba(0,0,0,0.08);"
    "}"
    ".scenery-settings-card label { margin-right: 12px; }"
    ".scenery-unit { color: alpha(currentColor, 0.55); }");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static gboolean
scenery_pending_is_aqua (OozeSceneryWindow *self)
{
  return !self->pending_wallpaper ||
         g_strcmp0 (self->pending_wallpaper, "aqua") == 0 ||
         g_strcmp0 (self->pending_wallpaper, "aqua-dark") == 0;
}

static void
scenery_load_pending_pixbuf (OozeSceneryWindow *self)
{
  g_clear_object (&self->selected_pixbuf);
  if (scenery_pending_is_aqua (self))
    return;
  self->selected_pixbuf =
    gdk_pixbuf_new_from_file_at_scale (self->pending_wallpaper, 640, 400,
                                       TRUE, NULL);
}

static void
scenery_load_pending (OozeSceneryWindow *self)
{
  g_autofree char *uri =
    g_settings_get_string (self->background_settings, "picture-uri");

  g_clear_pointer (&self->pending_wallpaper, g_free);
  if (!uri || !*uri)
    self->pending_wallpaper = g_strdup ("aqua");
  else
    {
      g_autofree char *path = g_filename_from_uri (uri, NULL, NULL);
      self->pending_wallpaper = path ? g_steal_pointer (&path)
                                     : g_strdup ("aqua");
    }

  g_clear_pointer (&self->pending_mode, g_free);
  self->pending_mode =
    g_settings_get_string (self->scenery_settings, "screensaver-mode");
  self->pending_idle =
    g_settings_get_uint (self->session_settings, "idle-delay");
  self->pending_lock_enabled =
    g_settings_get_boolean (self->screensaver_settings, "lock-enabled");
  self->pending_lock_delay =
    g_settings_get_uint (self->screensaver_settings, "lock-delay");
}

static void
scenery_draw_aqua (cairo_t *cr,
                   int      width,
                   int      height,
                   gboolean dark)
{
  cairo_pattern_t *gradient;
  int y;

  gradient = cairo_pattern_create_radial (width * 0.52, height * 0.32, 0,
                                          width * 0.52, height * 0.32,
                                          hypot (width, height) * 0.80);
  cairo_pattern_add_color_stop_rgb (gradient, 0,
                                     dark ? 0.18 : 0.52,
                                     dark ? 0.20 : 0.68,
                                     dark ? 0.28 : 0.92);
  cairo_pattern_add_color_stop_rgb (gradient, 0.55,
                                     dark ? 0.12 : 0.34,
                                     dark ? 0.14 : 0.52,
                                     dark ? 0.20 : 0.82);
  cairo_pattern_add_color_stop_rgb (gradient, 1,
                                     dark ? 0.06 : 0.16,
                                     dark ? 0.07 : 0.30,
                                     dark ? 0.11 : 0.58);
  cairo_set_source (cr, gradient);
  cairo_paint (cr);
  cairo_pattern_destroy (gradient);

  cairo_set_source_rgba (cr, 1, 1, 1, dark ? 0.025 : 0.05);
  for (y = 0; y < height; y += 4)
    cairo_rectangle (cr, 0, y, width, 1);
  cairo_fill (cr);
}

static void
scenery_draw_wallpaper (GtkDrawingArea *area,
                        cairo_t        *cr,
                        int             width,
                        int             height,
                        gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);

  if (self->selected_pixbuf)
    {
      double sx = (double) width / gdk_pixbuf_get_width (self->selected_pixbuf);
      double sy = (double) height / gdk_pixbuf_get_height (self->selected_pixbuf);
      double scale = MAX (sx, sy);
      double draw_width = gdk_pixbuf_get_width (self->selected_pixbuf) * scale;
      double draw_height = gdk_pixbuf_get_height (self->selected_pixbuf) * scale;

      cairo_save (cr);
      cairo_translate (cr, (width - draw_width) / 2, (height - draw_height) / 2);
      cairo_scale (cr, scale, scale);
      gdk_cairo_set_source_pixbuf (cr, self->selected_pixbuf, 0, 0);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else
    scenery_draw_aqua (cr, width, height,
                       g_strcmp0 (self->pending_wallpaper, "aqua-dark") == 0
                         ? TRUE : ooze_theme_is_dark ());

  (void) area;
}

static gboolean
scenery_mode_is_hack (const char *mode)
{
  return mode != NULL && g_str_has_prefix (mode, "xscreensaver:");
}

static void
scenery_draw_hack_placeholder (cairo_t    *cr,
                               int         width,
                               int         height,
                               const char *mode)
{
  const char *hack = mode + strlen ("xscreensaver:");
  cairo_text_extents_t extents;

  cairo_save (cr);
  cairo_set_source_rgba (cr, 0.02, 0.05, 0.12, 0.82);
  cairo_paint (cr);

  cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size (cr, MAX (12.0, height * 0.10));
  cairo_text_extents (cr, hack, &extents);
  cairo_set_source_rgba (cr, 0.85, 0.90, 1.0, 0.95);
  cairo_move_to (cr, (width - extents.width) / 2.0, height * 0.48);
  cairo_show_text (cr, hack);

  cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr, MAX (9.0, height * 0.055));
  {
    const char *note = "XScreenSaver — plays when the saver starts";

    cairo_text_extents (cr, note, &extents);
    cairo_set_source_rgba (cr, 0.65, 0.72, 0.88, 0.9);
    cairo_move_to (cr, (width - extents.width) / 2.0, height * 0.62);
    cairo_show_text (cr, note);
  }
  cairo_restore (cr);
}

static void
scenery_draw_screensaver (GtkDrawingArea *area,
                          cairo_t        *cr,
                          int             width,
                          int             height,
                          gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  gboolean animated = g_strcmp0 (self->pending_mode, "none") != 0;

  scenery_draw_wallpaper (NULL, cr, width, height, self);
  if (scenery_mode_is_hack (self->pending_mode))
    scenery_draw_hack_placeholder (cr, width, height, self->pending_mode);
  else if (animated && self->flow_surface)
    {
      cairo_save (cr);
      cairo_scale (cr,
                   (double) width / self->flow_width,
                   (double) height / self->flow_height);
      cairo_set_source_surface (cr, self->flow_surface, 0, 0);
      cairo_paint (cr);
      cairo_restore (cr);
    }

  (void) area;
}

static void
scenery_mode_tile_draw (GtkDrawingArea *area,
                        cairo_t        *cr,
                        int             width,
                        int             height,
                        gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  const char *mode = g_object_get_data (G_OBJECT (area), "mode");

  scenery_draw_wallpaper (NULL, cr, width, height, self);
  if (mode && g_strcmp0 (mode, "none") != 0)
    {
      cairo_surface_t *surface =
        cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);

      ooze_flow_render_scene (surface, width, height, 2.4,
                              ooze_theme_is_dark (), mode);
      cairo_surface_mark_dirty (surface);
      cairo_set_source_surface (cr, surface, 0, 0);
      cairo_paint (cr);
      cairo_surface_destroy (surface);
    }
}

static void
scenery_flow_resize (OozeSceneryWindow *self,
                     int                width,
                     int                height)
{
  if (width == self->flow_width && height == self->flow_height &&
      self->flow_surface)
    return;

  if (self->flow_surface)
    cairo_surface_destroy (self->flow_surface);
  self->flow_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                   MAX (64, width / 2),
                                                   MAX (36, height / 2));
  self->flow_width = MAX (64, width / 2);
  self->flow_height = MAX (36, height / 2);
  self->flow_last_frame_us = 0;
}

static gboolean
scenery_flow_tick (GtkWidget     *widget,
                   GdkFrameClock *clock,
                   gpointer       user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  gint64 now = gdk_frame_clock_get_frame_time (clock);

  if (self->page != SCENERY_PAGE_SCREENSAVER ||
      g_strcmp0 (self->pending_mode, "none") == 0 ||
      scenery_mode_is_hack (self->pending_mode))
    return G_SOURCE_CONTINUE;

  scenery_flow_resize (self,
                       gtk_widget_get_width (widget),
                       gtk_widget_get_height (widget));
  if (self->flow_last_frame_us != 0 &&
      now - self->flow_last_frame_us < 33000)
    return G_SOURCE_CONTINUE;

  self->flow_last_frame_us = now;
  self->flow_phase += 0.012;
  {
    cairo_t *cr = cairo_create (self->flow_surface);

    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_destroy (cr);
  }
  ooze_flow_render_scene (self->flow_surface,
                          self->flow_width,
                          self->flow_height,
                          self->flow_phase,
                          ooze_theme_is_dark (),
                          self->pending_mode);
  cairo_surface_mark_dirty (self->flow_surface);
  gtk_widget_queue_draw (widget);
  return G_SOURCE_CONTINUE;
}

static void
scenery_set_wallpaper (OozeSceneryWindow *self,
                       const char        *uri)
{
  g_clear_pointer (&self->pending_wallpaper, g_free);
  self->pending_wallpaper = g_strdup (uri ? uri : "aqua");
  scenery_load_pending_pixbuf (self);
  scenery_refresh_selection (self);
  scenery_refresh_preview (self);
  scenery_mark_dirty (self);
}

static void
on_wallpaper_tile_clicked (GtkButton *button,
                           gpointer   user_data)
{
  scenery_set_wallpaper (OOZE_SCENERY_WINDOW (user_data),
                         g_object_get_data (G_OBJECT (button), "uri"));
}

static void
wallpaper_tile_free (gpointer data)
{
  WallpaperTile *tile = data;

  g_clear_object (&tile->pixbuf);
  g_free (tile->uri);
  g_free (tile);
}

static void
scenery_tile_draw (GtkDrawingArea *area,
                   cairo_t        *cr,
                   int             width,
                   int             height,
                   gpointer        user_data)
{
  WallpaperTile *tile = user_data;

  if (tile->pixbuf)
    {
      double scale = MAX ((double) width / gdk_pixbuf_get_width (tile->pixbuf),
                          (double) height / gdk_pixbuf_get_height (tile->pixbuf));
      cairo_save (cr);
      cairo_translate (cr,
                       (width - gdk_pixbuf_get_width (tile->pixbuf) * scale) / 2,
                       (height - gdk_pixbuf_get_height (tile->pixbuf) * scale) / 2);
      cairo_scale (cr, scale, scale);
      gdk_cairo_set_source_pixbuf (cr, tile->pixbuf, 0, 0);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else
    scenery_draw_aqua (cr, width, height,
                       g_strcmp0 (tile->uri, "aqua-dark") == 0);

  (void) area;
}

static GtkWidget *
scenery_make_tile (OozeSceneryWindow *self,
                   const char        *label,
                   const char        *uri,
                   GdkPixbuf         *pixbuf)
{
  WallpaperTile *tile = g_new0 (WallpaperTile, 1);
  GtkWidget *button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *caption = gtk_label_new (label);

  tile->button = button;
  tile->uri = g_strdup (uri);
  tile->pixbuf = pixbuf ? g_object_ref (pixbuf) : NULL;
  gtk_widget_set_size_request (preview, 128, 82);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_tile_draw, tile, NULL);
  gtk_box_append (GTK_BOX (box), preview);
  gtk_box_append (GTK_BOX (box), caption);
  gtk_widget_add_css_class (box, "spot-grid-cell");
  gtk_widget_add_css_class (caption, "spot-grid-label");
  gtk_label_set_xalign (GTK_LABEL (caption), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (caption), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (caption), 12);
  gtk_label_set_lines (GTK_LABEL (caption), 2);
  gtk_label_set_wrap (GTK_LABEL (caption), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (caption), PANGO_WRAP_WORD_CHAR);
  gtk_widget_set_tooltip_text (button, label);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_widget_add_css_class (button, "scenery-tile");
  g_object_set_data_full (G_OBJECT (button), "uri", g_strdup (uri), g_free);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (on_wallpaper_tile_clicked), self);
  g_object_set_data_full (G_OBJECT (button), "tile", tile,
                          wallpaper_tile_free);
  return button;
}

static GtkWidget *
scenery_make_choose_tile (OozeSceneryWindow *self)
{
  static const char *icons[] = {
    "folder-open", "document-open", "image-x-generic", NULL
  };
  GtkWidget *button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  GtkWidget *icon = ooze_icon_image_new (icons, OOZE_ICON_SIZE_GRID);
  GtkWidget *label = gtk_label_new ("Choose…");

  gtk_widget_add_css_class (box, "spot-grid-cell");
  gtk_widget_add_css_class (label, "spot-grid-label");
  gtk_label_set_xalign (GTK_LABEL (label), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_lines (GTK_LABEL (label), 2);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_box_append (GTK_BOX (box), icon);
  gtk_box_append (GTK_BOX (box), label);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_widget_add_css_class (button, "scenery-action-tile");
  gtk_widget_set_tooltip_text (button, "Choose an image from your files");
  g_signal_connect (button, "clicked", G_CALLBACK (on_choose_clicked), self);
  return button;
}

static void
scenery_refresh_selection_in_box (GtkWidget   *box,
                                  const char  *pending)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (box);
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *tile = gtk_flow_box_child_get_child (
        GTK_FLOW_BOX_CHILD (child));
      const char *tile_uri = tile
        ? g_object_get_data (G_OBJECT (tile), "uri") : NULL;
      gboolean selected = tile_uri && g_strcmp0 (tile_uri, pending) == 0;

      gtk_widget_set_state_flags (child, selected ? GTK_STATE_FLAG_CHECKED : 0,
                                  TRUE);
    }
}

static GtkWidget *scenery_make_choose_tile (OozeSceneryWindow *self);

static void
scenery_refresh_custom_tile (OozeSceneryWindow *self)
{
  GtkWidget *child;

  if (!self->custom_tiles)
    return;

  for (child = gtk_widget_get_first_child (self->custom_tiles);
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *tile = gtk_flow_box_child_get_child (
        GTK_FLOW_BOX_CHILD (child));

      if (tile && g_object_get_data (G_OBJECT (tile), "custom-tile"))
        {
          gtk_flow_box_remove (GTK_FLOW_BOX (self->custom_tiles), child);
          break;
        }
    }

  if (scenery_pending_is_aqua (self) ||
      g_str_has_prefix (self->pending_wallpaper, "/usr/share/backgrounds/"))
    return;

  {
    g_autofree char *name = g_path_get_basename (self->pending_wallpaper);
    GtkWidget *tile = scenery_make_tile (self, name, self->pending_wallpaper,
                                         self->selected_pixbuf);

    g_object_set_data (G_OBJECT (tile), "custom-tile", GINT_TO_POINTER (1));
    gtk_flow_box_insert (GTK_FLOW_BOX (self->custom_tiles), tile, 0);
  }
}

static void
scenery_refresh_selection (OozeSceneryWindow *self)
{
  scenery_refresh_custom_tile (self);
  scenery_refresh_selection_in_box (self->wallpaper_tiles,
                                    self->pending_wallpaper);
  scenery_refresh_selection_in_box (self->ubuntu_tiles,
                                    self->pending_wallpaper);
  scenery_refresh_selection_in_box (self->custom_tiles,
                                    self->pending_wallpaper);
}

static void
scenery_refresh_preview (OozeSceneryWindow *self)
{
  if (self->wallpaper_preview)
    gtk_widget_queue_draw (self->wallpaper_preview);
  if (self->screensaver_preview)
    gtk_widget_queue_draw (self->screensaver_preview);
}

static void
scenery_refresh_mode_selection (OozeSceneryWindow *self)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (self->screensaver_mode_tiles);
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *tile = gtk_flow_box_child_get_child (
        GTK_FLOW_BOX_CHILD (child));
      const char *child_mode = tile
        ? g_object_get_data (G_OBJECT (tile), "mode") : NULL;
      gtk_widget_set_state_flags (
        child, g_strcmp0 (self->pending_mode, child_mode) == 0
          ? GTK_STATE_FLAG_CHECKED : 0, TRUE);
    }

  if (self->hack_dropdown)
    {
      guint position = GTK_INVALID_LIST_POSITION;
      gboolean was_loading = self->loading;

      if (scenery_mode_is_hack (self->pending_mode))
        {
          const char *hack =
            self->pending_mode + strlen ("xscreensaver:");
          guint n = g_list_model_get_n_items (
            G_LIST_MODEL (self->hack_names));
          guint i;

          for (i = 0; i < n; i++)
            if (g_strcmp0 (gtk_string_list_get_string (self->hack_names, i),
                           hack) == 0)
              {
                position = i;
                break;
              }
        }

      self->loading = TRUE;
      gtk_drop_down_set_selected (GTK_DROP_DOWN (self->hack_dropdown),
                                  position);
      self->loading = was_loading;
    }
}

static void
on_screensaver_mode_clicked (GtkButton *button,
                             gpointer   user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  const char *mode = g_object_get_data (G_OBJECT (button), "mode");

  g_clear_pointer (&self->pending_mode, g_free);
  self->pending_mode = g_strdup (mode);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);
  scenery_mark_dirty (self);
}

static void
scenery_reload (OozeSceneryWindow *self)
{
  self->loading = TRUE;
  scenery_load_pending (self);
  scenery_load_pending_pixbuf (self);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->start_after),
                             self->pending_idle);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (self->lock_enabled),
                               self->pending_lock_enabled);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->lock_after),
                             self->pending_lock_delay);
  scenery_refresh_selection (self);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);
  self->loading = FALSE;
  scenery_set_dirty (self, FALSE);
}

static void
scenery_apply (OozeSceneryWindow *self)
{
  gtk_spin_button_update (GTK_SPIN_BUTTON (self->start_after));
  gtk_spin_button_update (GTK_SPIN_BUTTON (self->lock_after));
  self->pending_idle =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->start_after));
  self->pending_lock_delay =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (self->lock_after));

  if (scenery_pending_is_aqua (self))
    {
      g_settings_set_string (self->background_settings, "picture-uri", "");
      g_settings_set_string (self->background_settings, "picture-uri-dark", "");
    }
  else
    {
      g_autofree char *uri =
        g_filename_to_uri (self->pending_wallpaper, NULL, NULL);

      if (uri)
        {
          g_settings_set_string (self->background_settings, "picture-uri", uri);
          g_settings_set_string (self->background_settings,
                                 "picture-uri-dark", uri);
          g_settings_set_string (self->background_settings,
                                 "picture-options", "zoom");
        }
    }

  g_settings_set_string (self->scenery_settings, "screensaver-mode",
                         self->pending_mode);
  g_settings_set_uint (self->session_settings, "idle-delay",
                       self->pending_idle);
  g_settings_set_boolean (self->screensaver_settings, "lock-enabled",
                          self->pending_lock_enabled);
  g_settings_set_uint (self->screensaver_settings, "lock-delay",
                       self->pending_lock_delay);
  scenery_set_dirty (self, FALSE);
}

static void
on_apply_clicked (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
  scenery_apply (OOZE_SCENERY_WINDOW (user_data));
}

static void
on_cancel_clicked (GtkButton *button G_GNUC_UNUSED,
                   gpointer   user_data)
{
  scenery_reload (OOZE_SCENERY_WINDOW (user_data));
}

static void
on_setting_changed (GSettings  *settings G_GNUC_UNUSED,
                    const char *key G_GNUC_UNUSED,
                    gpointer    user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);

  /* Ignore external changes while the user has unsaved staged edits. */
  if (self->dirty || self->loading)
    return;
  scenery_reload (self);
}

static void
on_start_after_changed (GtkSpinButton *spin,
                        gpointer       user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);

  self->pending_idle = gtk_spin_button_get_value_as_int (spin);
  scenery_mark_dirty (self);
}

static void
on_spin_activate (GtkSpinButton *spin,
                  gpointer       user_data G_GNUC_UNUSED)
{
  gtk_spin_button_update (spin);
}

static void
on_lock_after_changed (GtkSpinButton *spin,
                       gpointer       user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);

  self->pending_lock_delay = gtk_spin_button_get_value_as_int (spin);
  scenery_mark_dirty (self);
}

static void
on_lock_enabled_changed (GtkCheckButton *button,
                         gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);

  self->pending_lock_enabled = gtk_check_button_get_active (button);
  scenery_mark_dirty (self);
}

static GtkWidget *
scenery_section_label (const char *text)
{
  GtkWidget *label = gtk_label_new (text);
  gtk_widget_add_css_class (label, "scenery-section");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  return label;
}

static GtkWidget *
scenery_build_wallpaper_page (OozeSceneryWindow *self)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *group;

  gtk_widget_add_css_class (page, "scenery-page");
  self->wallpaper_preview = preview;
  gtk_widget_add_css_class (preview, "scenery-preview");
  gtk_widget_set_size_request (preview, 320, 170);
  gtk_widget_set_hexpand (preview, FALSE);
  gtk_widget_set_halign (preview, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_draw_wallpaper, self, NULL);
  gtk_box_append (GTK_BOX (page), preview);
  gtk_widget_set_hexpand (scroll, TRUE);
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), content);
  gtk_box_append (GTK_BOX (page), scroll);

  gtk_box_append (GTK_BOX (content), scenery_section_label ("Ooze Aqua"));
  self->wallpaper_tiles = gtk_flow_box_new ();
  gtk_widget_add_css_class (self->wallpaper_tiles, "scenery-grid");
  gtk_widget_set_halign (self->wallpaper_tiles, GTK_ALIGN_START);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->wallpaper_tiles), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (self->wallpaper_tiles), 12);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (self->wallpaper_tiles), 12);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->wallpaper_tiles), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->wallpaper_tiles), 6);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->wallpaper_tiles),
                                   GTK_SELECTION_NONE);
  gtk_flow_box_append (GTK_FLOW_BOX (self->wallpaper_tiles),
                       scenery_make_tile (self, "Light", "aqua", NULL));
  gtk_flow_box_append (GTK_FLOW_BOX (self->wallpaper_tiles),
                       scenery_make_tile (self, "Dark", "aqua-dark", NULL));
  gtk_box_append (GTK_BOX (content), self->wallpaper_tiles);

  gtk_box_append (GTK_BOX (content), scenery_section_label ("Ubuntu"));
  self->ubuntu_tiles = gtk_flow_box_new ();
  gtk_widget_add_css_class (self->ubuntu_tiles, "scenery-grid");
  gtk_widget_set_halign (self->ubuntu_tiles, GTK_ALIGN_START);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->ubuntu_tiles), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (self->ubuntu_tiles), 12);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (self->ubuntu_tiles), 12);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->ubuntu_tiles), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->ubuntu_tiles), 6);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->ubuntu_tiles),
                                   GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (content), self->ubuntu_tiles);

  gtk_box_append (GTK_BOX (content), scenery_section_label ("Your images"));
  group = gtk_flow_box_new ();
  self->custom_tiles = group;
  gtk_widget_add_css_class (group, "scenery-grid");
  gtk_widget_set_halign (group, GTK_ALIGN_START);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (group), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (group), 12);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (group), 12);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (group), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (group), 6);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (group), GTK_SELECTION_NONE);
  gtk_flow_box_append (GTK_FLOW_BOX (group), scenery_make_choose_tile (self));
  gtk_box_append (GTK_BOX (content), group);
  return page;
}

static void
scenery_add_ubuntu_images (OozeSceneryWindow *self)
{
  g_autoptr (GDir) dir = g_dir_open ("/usr/share/backgrounds", 0, NULL);
  const char *name;

  if (!dir)
    return;
  while ((name = g_dir_read_name (dir)))
    {
      g_autofree char *path = NULL;
      g_autoptr (GdkPixbuf) pixbuf = NULL;

      if (!g_str_has_suffix (name, ".jpg") &&
          !g_str_has_suffix (name, ".jpeg") &&
          !g_str_has_suffix (name, ".png"))
        continue;
      path = g_build_filename ("/usr/share/backgrounds", name, NULL);
      pixbuf = gdk_pixbuf_new_from_file_at_scale (path, 240, 140, TRUE, NULL);
      if (!pixbuf)
        continue;
      gtk_flow_box_append (GTK_FLOW_BOX (self->ubuntu_tiles),
                           scenery_make_tile (self, name, path, pixbuf));
    }
}

static void
on_choose_finish (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file =
    gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, &error);

  if (file)
    {
      g_autofree char *path = g_file_get_path (file);
      if (path)
        scenery_set_wallpaper (self, path);
    }
  g_object_unref (source);
}

static void
on_choose_clicked (GtkButton *button G_GNUC_UNUSED,
                   gpointer   user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  GtkFileDialog *dialog = gtk_file_dialog_new ();

  gtk_file_dialog_set_title (dialog, "Choose Wallpaper");
  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL,
                        on_choose_finish, self);
}

static GtkWidget *
scenery_mode_tile (OozeSceneryWindow *self,
                   const char        *label,
                   const char        *mode)
{
  GtkWidget *button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *title = gtk_label_new (label);

  gtk_widget_set_size_request (preview, 128, 82);
  g_object_set_data (G_OBJECT (preview), "mode", (gpointer) mode);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_mode_tile_draw, self, NULL);
  gtk_widget_add_css_class (box, "spot-grid-cell");
  gtk_widget_add_css_class (title, "spot-grid-label");
  gtk_label_set_xalign (GTK_LABEL (title), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_label_set_lines (GTK_LABEL (title), 2);
  gtk_label_set_wrap (GTK_LABEL (title), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (title), PANGO_WRAP_WORD_CHAR);
  gtk_box_append (GTK_BOX (box), preview);
  gtk_box_append (GTK_BOX (box), title);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_widget_add_css_class (button, "scenery-tile");
  gtk_widget_set_tooltip_text (button, label);
  g_object_set_data (G_OBJECT (button), "mode", (gpointer) mode);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (on_screensaver_mode_clicked), self);
  return button;
}

/* Friendly display labels for well-known XScreenSaver modules; anything
 * else falls back to its binary name. */
typedef struct
{
  const char *bin;
  const char *label;
} OozeCuratedHack;

static const OozeCuratedHack ooze_curated_hacks[] = {
  { "glmatrix",     "GLMatrix" },
  { "galaxy",       "Galaxy" },
  { "bsod",         "BSOD" },
  { "atlantis",     "Atlantis" },
  { "flurry",       "Flurry" },
  { "apple2",       "Apple ][" },
  { "xanalogtv",    "XAnalogTV" },
  { "starwars",     "Star Wars" },
  { "phosphor",     "Phosphor" },
  { "sonar",        "Sonar" },
  { "deluxe",       "Deluxe" },
  { "anemone",      "Anemone" },
  { "distort",      "Distort" },
  { "interference", "Interference" },
  { "deco",         "Deco" },
  { "crackberg",    "Crackberg" },
  { "endgame",      "Endgame" },
  { "hyphae",       "Hyphae" },
  { "jigsaw",       "Jigsaw" },
  { "lavalite",     "Lavalite" },
};

static const char *ooze_hack_dirs[] = {
  "/usr/libexec/xscreensaver",
  "/usr/lib/xscreensaver",
  "/usr/lib/misc/xscreensaver",
};

static const char *
scenery_hack_label (const char *bin)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (ooze_curated_hacks); i++)
    if (g_strcmp0 (ooze_curated_hacks[i].bin, bin) == 0)
      return ooze_curated_hacks[i].label;
  return bin;
}

/* Returns every installed XScreenSaver module, sorted by name. */
static GStrv
scenery_list_hacks (void)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr (GPtrArray) names =
    g_ptr_array_new_with_free_func (g_free);
  gsize i;
  guint j;

  for (i = 0; i < G_N_ELEMENTS (ooze_hack_dirs); i++)
    {
      g_autoptr (GDir) dir = g_dir_open (ooze_hack_dirs[i], 0, NULL);
      const char *entry;

      if (!dir)
        continue;
      while ((entry = g_dir_read_name (dir)) != NULL)
        {
          g_autofree char *path =
            g_build_filename (ooze_hack_dirs[i], entry, NULL);
          guint k;
          gboolean seen = FALSE;

          if (!g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
            continue;
          for (k = 0; k < names->len && !seen; k++)
            seen = g_strcmp0 (g_ptr_array_index (names, k), entry) == 0;
          if (!seen)
            g_ptr_array_add (names, g_strdup (entry));
        }
    }

  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
  for (j = 0; j < names->len; j++)
    g_strv_builder_add (builder, g_ptr_array_index (names, j));
  return g_strv_builder_end (builder);
}

static void
on_hack_selected (GObject    *dropdown,
                  GParamSpec *pspec G_GNUC_UNUSED,
                  gpointer    user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  guint selected;
  const char *hack;

  if (self->loading)
    return;

  selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (dropdown));
  if (selected == GTK_INVALID_LIST_POSITION)
    return;

  hack = gtk_string_list_get_string (self->hack_names, selected);
  if (!hack)
    return;

  g_clear_pointer (&self->pending_mode, g_free);
  self->pending_mode = g_strdup_printf ("xscreensaver:%s", hack);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);
  scenery_mark_dirty (self);
}

static GtkWidget *
scenery_build_hack_row (OozeSceneryWindow *self)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  g_auto (GStrv) hacks = scenery_list_hacks ();
  GtkWidget *hint;

  gtk_widget_add_css_class (row, "scenery-settings-card");

  if (!hacks || !hacks[0])
    {
      hint = gtk_label_new ("No XScreenSaver modules found. Install the "
                            "xscreensaver-data and xscreensaver-data-extra "
                            "packages to unlock them.");
      gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
      gtk_label_set_xalign (GTK_LABEL (hint), 0);
      gtk_box_append (GTK_BOX (row), hint);
      return row;
    }

  hint = gtk_label_new ("Pick a classic XScreenSaver module instead of an "
                        "Ooze scene. It runs full screen when the "
                        "screensaver starts.");
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_label_set_xalign (GTK_LABEL (hint), 0);
  gtk_box_append (GTK_BOX (row), hint);

  self->hack_names = gtk_string_list_new ((const char * const *) hacks);
  {
    GtkStringList *labels = gtk_string_list_new (NULL);
    guint i;

    for (i = 0; hacks[i] != NULL; i++)
      gtk_string_list_append (labels, scenery_hack_label (hacks[i]));
    self->hack_dropdown =
      gtk_drop_down_new (G_LIST_MODEL (labels), NULL);
  }
  gtk_drop_down_set_enable_search (GTK_DROP_DOWN (self->hack_dropdown),
                                   TRUE);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->hack_dropdown),
                              GTK_INVALID_LIST_POSITION);
  gtk_widget_set_halign (self->hack_dropdown, GTK_ALIGN_START);
  g_signal_connect (self->hack_dropdown, "notify::selected",
                    G_CALLBACK (on_hack_selected), self);
  gtk_box_append (GTK_BOX (row), self->hack_dropdown);

  return row;
}

static GtkWidget *
scenery_build_screensaver_page (OozeSceneryWindow *self)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *modes = gtk_flow_box_new ();
  GtkWidget *settings = gtk_grid_new ();
  GtkWidget *label;

  gtk_widget_add_css_class (page, "scenery-page");
  self->screensaver_preview = preview;
  gtk_widget_add_css_class (preview, "scenery-preview");
  gtk_widget_set_size_request (preview, 380, 210);
  gtk_widget_set_hexpand (preview, FALSE);
  gtk_widget_set_halign (preview, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_draw_screensaver, self, NULL);
  gtk_box_append (GTK_BOX (page), preview);
  gtk_widget_set_hexpand (scroll, TRUE);
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), content);
  gtk_box_append (GTK_BOX (page), scroll);
  gtk_box_append (GTK_BOX (content), scenery_section_label ("Screensaver mode"));
  self->screensaver_mode_tiles = modes;
  gtk_widget_add_css_class (modes, "scenery-grid");
  gtk_widget_set_halign (modes, GTK_ALIGN_START);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (modes), TRUE);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (modes), 12);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (modes), 12);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (modes), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (modes), 4);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (modes), GTK_SELECTION_NONE);
  gtk_flow_box_append (GTK_FLOW_BOX (modes),
                       scenery_mode_tile (self, "None", "none"));
  gtk_box_append (GTK_BOX (content), modes);

  gtk_box_append (GTK_BOX (content),
                  scenery_section_label ("XScreenSaver"));
  gtk_box_append (GTK_BOX (content), scenery_build_hack_row (self));

  label = scenery_section_label ("Timings");
  gtk_box_append (GTK_BOX (content), label);
  gtk_widget_add_css_class (settings, "scenery-settings-card");
  gtk_grid_set_row_spacing (GTK_GRID (settings), 8);
  gtk_grid_set_column_spacing (GTK_GRID (settings), 14);
  gtk_widget_set_hexpand (settings, TRUE);
  self->start_after = gtk_spin_button_new_with_range (0, 86400, 1);
  gtk_spin_button_set_value (
    GTK_SPIN_BUTTON (self->start_after),
    g_settings_get_uint (self->session_settings, "idle-delay"));
  label = gtk_label_new ("Start after");
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_grid_attach (GTK_GRID (settings), label, 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (settings), self->start_after, 1, 0, 1, 1);
  gtk_widget_set_hexpand (self->start_after, FALSE);
  label = gtk_label_new ("seconds");
  gtk_widget_add_css_class (label, "scenery-unit");
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_grid_attach (GTK_GRID (settings), label, 2, 0, 1, 1);
  self->lock_enabled = gtk_check_button_new_with_label ("Lock screen");
  gtk_check_button_set_active (
    GTK_CHECK_BUTTON (self->lock_enabled),
    g_settings_get_boolean (self->screensaver_settings, "lock-enabled"));
  gtk_grid_attach (GTK_GRID (settings), self->lock_enabled, 0, 1, 2, 1);
  self->lock_after = gtk_spin_button_new_with_range (0, 86400, 1);
  gtk_spin_button_set_value (
    GTK_SPIN_BUTTON (self->lock_after),
    g_settings_get_uint (self->screensaver_settings, "lock-delay"));
  label = gtk_label_new ("Lock after");
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_grid_attach (GTK_GRID (settings), label, 0, 2, 1, 1);
  gtk_grid_attach (GTK_GRID (settings), self->lock_after, 1, 2, 1, 1);
  label = gtk_label_new ("seconds");
  gtk_widget_add_css_class (label, "scenery-unit");
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_grid_attach (GTK_GRID (settings), label, 2, 2, 1, 1);
  gtk_box_append (GTK_BOX (content), settings);
  g_signal_connect (self->start_after, "value-changed",
                    G_CALLBACK (on_start_after_changed), self);
  g_signal_connect (self->start_after, "activate",
                    G_CALLBACK (on_spin_activate), self);
  g_signal_connect (self->lock_enabled, "toggled",
                    G_CALLBACK (on_lock_enabled_changed), self);
  g_signal_connect (self->lock_after, "value-changed",
                    G_CALLBACK (on_lock_after_changed), self);
  g_signal_connect (self->lock_after, "activate",
                    G_CALLBACK (on_spin_activate), self);
  return page;
}

static void
scenery_show_page (OozeSceneryWindow *self,
                   SceneryPage        page)
{
  self->page = page;
  gtk_stack_set_visible_child_name (
    GTK_STACK (self->stack),
    page == SCENERY_PAGE_SCREENSAVER ? "screensaver" : "wallpaper");
  ooze_button_set_toggled (self->nav_wallpaper,
                           page == SCENERY_PAGE_WALLPAPER);
  ooze_button_set_toggled (self->nav_screensaver,
                           page == SCENERY_PAGE_SCREENSAVER);
}

static void
on_nav_wallpaper_clicked (GtkButton *button G_GNUC_UNUSED,
                          gpointer   user_data)
{
  scenery_show_page (OOZE_SCENERY_WINDOW (user_data),
                     SCENERY_PAGE_WALLPAPER);
}

static void
on_nav_screensaver_clicked (GtkButton *button G_GNUC_UNUSED,
                            gpointer   user_data)
{
  scenery_show_page (OOZE_SCENERY_WINDOW (user_data),
                     SCENERY_PAGE_SCREENSAVER);
}

static void
scenery_action_about (GSimpleAction *action G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Scenery",
                      "org.ooze.Scenery",
                      "Wallpaper and screensaver settings for Ooze Desktop.",
                      OOZE_VERSION);
}

static void
ooze_scenery_window_dispose (GObject *object)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (object);

  if (self->flow_tick_id)
    {
      gtk_widget_remove_tick_callback (self->screensaver_preview,
                                       self->flow_tick_id);
      self->flow_tick_id = 0;
    }
  g_clear_pointer (&self->flow_surface, cairo_surface_destroy);
  g_clear_object (&self->selected_pixbuf);
  g_clear_pointer (&self->pending_wallpaper, g_free);
  g_clear_pointer (&self->pending_mode, g_free);
  g_clear_object (&self->background_settings);
  g_clear_object (&self->scenery_settings);
  g_clear_object (&self->session_settings);
  g_clear_object (&self->screensaver_settings);
  G_OBJECT_CLASS (ooze_scenery_window_parent_class)->dispose (object);
}

static void
ooze_scenery_window_constructed (GObject *object)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (object);
  static const char * const wallpaper_icons[] = {
    "preferences-desktop-wallpaper", "image-x-generic",
    "folder-pictures", NULL
  };
  static const char * const screensaver_icons[] = {
    "preferences-desktop-screensaver", "video-display", NULL
  };
  static const char * const choose_icons[] = {
    "folder-open", "document-open", NULL
  };
  GtkWidget *shell;
  GtkWidget *shell_overlay;
  GtkWidget *toolbar;
  GtkWidget *toolbar_group;
  GtkWidget *choose_button;
  GtkWidget *apply_button;
  GtkWidget *cancel_button;
  GMenu *help;
  GSimpleAction *about;

  G_OBJECT_CLASS (ooze_scenery_window_parent_class)->constructed (object);
  ooze_toolbar_ensure_css ();
  ooze_scroll_ensure_css ();
  ooze_action_bar_ensure_css ();
  scenery_ensure_css ();
  self->background_settings = g_settings_new ("org.gnome.desktop.background");
  self->scenery_settings = g_settings_new ("org.ooze.scenery");
  self->session_settings = g_settings_new ("org.gnome.desktop.session");
  self->screensaver_settings = g_settings_new ("org.gnome.desktop.screensaver");
  self->page = SCENERY_PAGE_WALLPAPER;
  scenery_load_pending (self);

  gtk_window_set_default_size (GTK_WINDOW (self), 880, 640);
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Scenery");
  ooze_application_window_set_title (OOZE_APPLICATION_WINDOW (self),
                                      "Ooze Scenery");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  toolbar = ooze_toolbar_new ();
  toolbar_group = ooze_toolbar_add_group (toolbar);
  self->nav_wallpaper =
    ooze_button_new_toolbar (wallpaper_icons, "Wallpaper", "Wallpaper");
  g_signal_connect (self->nav_wallpaper, "clicked",
                    G_CALLBACK (on_nav_wallpaper_clicked), self);
  gtk_box_append (GTK_BOX (toolbar_group), self->nav_wallpaper);
  self->nav_screensaver =
    ooze_button_new_toolbar (screensaver_icons, "Screensaver", "Screensaver");
  g_signal_connect (self->nav_screensaver, "clicked",
                    G_CALLBACK (on_nav_screensaver_clicked), self);
  gtk_box_append (GTK_BOX (toolbar_group), self->nav_screensaver);
  ooze_toolbar_add_separator (toolbar);
  toolbar_group = ooze_toolbar_add_group (toolbar);
  choose_button =
    ooze_button_new_toolbar (choose_icons, "Choose…",
                             "Choose an image from your files");
  g_signal_connect (choose_button, "clicked",
                    G_CALLBACK (on_choose_clicked), self);
  gtk_box_append (GTK_BOX (toolbar_group), choose_button);
  ooze_toolbar_add_spacer (toolbar);
  gtk_box_append (GTK_BOX (shell), toolbar);

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_stack_add_named (GTK_STACK (self->stack),
                       scenery_build_wallpaper_page (self), "wallpaper");
  gtk_stack_add_named (GTK_STACK (self->stack),
                       scenery_build_screensaver_page (self), "screensaver");
  gtk_box_append (GTK_BOX (shell), self->stack);

  self->action_bar = ooze_action_bar_new (&cancel_button, &apply_button);
  g_signal_connect (cancel_button, "clicked",
                    G_CALLBACK (on_cancel_clicked), self);
  g_signal_connect (apply_button, "clicked",
                    G_CALLBACK (on_apply_clicked), self);
  gtk_box_append (GTK_BOX (shell), self->action_bar);

  shell_overlay = gtk_overlay_new ();
  gtk_overlay_set_child (GTK_OVERLAY (shell_overlay), shell);
  ooze_application_window_set_content (OOZE_APPLICATION_WINDOW (self),
                                       shell_overlay);
  scenery_show_page (self, SCENERY_PAGE_WALLPAPER);
  scenery_add_ubuntu_images (self);
  scenery_refresh_selection (self);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);
  scenery_set_dirty (self, FALSE);

  self->flow_tick_id =
    gtk_widget_add_tick_callback (self->screensaver_preview,
                                  scenery_flow_tick, self, NULL);
  g_signal_connect (self->background_settings, "changed::picture-uri",
                    G_CALLBACK (on_setting_changed), self);
  g_signal_connect (self->background_settings, "changed::picture-uri-dark",
                    G_CALLBACK (on_setting_changed), self);
  g_signal_connect (self->background_settings, "changed::picture-options",
                    G_CALLBACK (on_setting_changed), self);
  g_signal_connect (self->scenery_settings, "changed::screensaver-mode",
                    G_CALLBACK (on_setting_changed), self);

  help = g_menu_new ();
  g_menu_append (help, "About Ooze Scenery", "win.about");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", G_MENU_MODEL (help));
  g_object_unref (help);
  about = g_simple_action_new ("about", NULL);
  g_signal_connect (about, "activate",
                    G_CALLBACK (scenery_action_about), self);
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (about));
  g_object_unref (about);
}

static void
ooze_scenery_window_class_init (OozeSceneryWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_scenery_window_constructed;
  object_class->dispose = ooze_scenery_window_dispose;
}

static void
ooze_scenery_window_init (OozeSceneryWindow *self)
{
  (void) self;
}

GtkWidget *
ooze_scenery_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_SCENERY_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
