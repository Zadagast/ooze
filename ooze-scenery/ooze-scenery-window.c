#include "ooze-scenery-window.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-flow.h"
#include "ooze-shared-appmenu.h"
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
  GtkWidget *start_after;
  GtkWidget *lock_enabled;
  GtkWidget *lock_after;
  GtkWidget *tab_wallpaper;
  GtkWidget *tab_screensaver;
  GdkPixbuf *selected_pixbuf;
  char *selected_uri;
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
static void on_choose_clicked (GtkButton *button,
                               gpointer   user_data);

static void
scenery_load_selected_pixbuf (OozeSceneryWindow *self,
                              const char        *uri)
{
  g_autofree char *path = NULL;

  g_clear_object (&self->selected_pixbuf);
  if (!uri || !*uri)
    return;
  path = g_filename_from_uri (uri, NULL, NULL);
  if (path)
    self->selected_pixbuf =
      gdk_pixbuf_new_from_file_at_scale (path, 640, 400, TRUE, NULL);
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
    scenery_draw_aqua (cr, width, height, ooze_theme_is_dark ());

  (void) area;
}

static void
scenery_draw_screensaver (GtkDrawingArea *area,
                          cairo_t        *cr,
                          int             width,
                          int             height,
                          gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  g_autofree char *mode =
    g_settings_get_string (self->scenery_settings, "screensaver-mode");
  gboolean flow = g_strcmp0 (mode, "flow") == 0;

  scenery_draw_wallpaper (NULL, cr, width, height, self);
  if (flow && self->flow_surface)
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

  g_autofree char *mode =
    g_settings_get_string (self->scenery_settings, "screensaver-mode");

  if (self->page != SCENERY_PAGE_SCREENSAVER ||
      g_strcmp0 (mode, "flow") != 0)
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
  ooze_flow_render (self->flow_surface,
                    self->flow_width,
                    self->flow_height,
                    self->flow_phase,
                    ooze_theme_is_dark ());
  cairo_surface_mark_dirty (self->flow_surface);
  gtk_widget_queue_draw (widget);
  return G_SOURCE_CONTINUE;
}

static void
scenery_set_wallpaper (OozeSceneryWindow *self,
                       const char        *uri)
{
  if (!uri || g_strcmp0 (uri, "aqua") == 0 ||
      g_strcmp0 (uri, "aqua-dark") == 0)
    {
      g_settings_set_string (self->background_settings, "picture-uri", "");
      g_settings_set_string (self->background_settings, "picture-uri-dark", "");
      g_clear_pointer (&self->selected_uri, g_free);
      g_clear_object (&self->selected_pixbuf);
    }
  else
    {
      g_autofree char *uri_value = NULL;

      uri_value = g_filename_to_uri (uri, NULL, NULL);
      if (!uri_value)
        return;
      g_settings_set_string (self->background_settings, "picture-uri",
                             uri_value);
      g_settings_set_string (self->background_settings,
                             "picture-uri-dark", uri_value);
      g_settings_set_string (self->background_settings,
                             "picture-options", "zoom");
      g_free (self->selected_uri);
      self->selected_uri = g_strdup (uri);
      g_clear_object (&self->selected_pixbuf);
      g_set_object (&self->selected_pixbuf,
                    gdk_pixbuf_new_from_file_at_scale (uri, 640, 400,
                                                       TRUE, NULL));
    }

  scenery_refresh_selection (self);
  scenery_refresh_preview (self);
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
  GtkWidget *button = gtk_button_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *caption = gtk_label_new (label);

  tile->button = button;
  tile->uri = g_strdup (uri);
  tile->pixbuf = pixbuf ? g_object_ref (pixbuf) : NULL;
  gtk_widget_set_size_request (preview, 132, 78);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_tile_draw, tile, NULL);
  gtk_box_append (GTK_BOX (box), preview);
  gtk_box_append (GTK_BOX (box), caption);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_widget_add_css_class (button, "scenery-tile");
  g_object_set_data_full (G_OBJECT (button), "uri", g_strdup (uri), g_free);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (on_wallpaper_tile_clicked), self);
  g_object_set_data_full (G_OBJECT (button), "tile", tile,
                          wallpaper_tile_free);
  return button;
}

static void
scenery_refresh_selection_in_box (GtkWidget   *box,
                                  const char  *uri)
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
      gboolean selected = (!uri || !*uri) && g_strcmp0 (tile_uri, "aqua") == 0;

      if (uri && *uri && tile_uri)
        {
          g_autofree char *tile_uri_value = NULL;

          tile_uri_value = g_filename_to_uri (tile_uri, NULL, NULL);
          selected = g_strcmp0 (uri, tile_uri_value) == 0;
        }
      gtk_widget_set_state_flags (child, selected ? GTK_STATE_FLAG_CHECKED : 0,
                                  TRUE);
    }
}

static void
scenery_refresh_selection (OozeSceneryWindow *self)
{
  g_autofree char *uri =
    g_settings_get_string (self->background_settings, "picture-uri");

  g_clear_pointer (&self->selected_uri, g_free);
  self->selected_uri = g_strdup (uri);
  scenery_load_selected_pixbuf (self, uri);
  scenery_refresh_selection_in_box (self->wallpaper_tiles, uri);
  scenery_refresh_selection_in_box (self->ubuntu_tiles, uri);
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
  g_autofree char *mode =
    g_settings_get_string (self->scenery_settings, "screensaver-mode");
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (self->screensaver_mode_tiles);
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      const char *child_mode = g_object_get_data (G_OBJECT (child), "mode");
      gtk_widget_set_state_flags (
        child, g_strcmp0 (mode, child_mode) == 0
          ? GTK_STATE_FLAG_CHECKED : 0, TRUE);
    }
}

static void
on_screensaver_mode_clicked (GtkButton *button,
                             gpointer   user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  const char *mode = g_object_get_data (G_OBJECT (button), "mode");

  g_settings_set_string (self->scenery_settings, "screensaver-mode", mode);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);
}

static void
on_setting_changed (GSettings  *settings G_GNUC_UNUSED,
                    const char *key G_GNUC_UNUSED,
                    gpointer    user_data)
{
  scenery_refresh_selection (OOZE_SCENERY_WINDOW (user_data));
  scenery_refresh_mode_selection (OOZE_SCENERY_WINDOW (user_data));
  scenery_refresh_preview (OOZE_SCENERY_WINDOW (user_data));
}

static void
on_start_after_changed (GtkSpinButton *spin,
                        gpointer       user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  g_settings_set_uint (self->session_settings, "idle-delay",
                       gtk_spin_button_get_value_as_int (spin));
}

static void
on_lock_after_changed (GtkSpinButton *spin,
                       gpointer       user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  g_settings_set_uint (self->screensaver_settings, "lock-delay",
                       gtk_spin_button_get_value_as_int (spin));
}

static void
on_lock_enabled_changed (GtkCheckButton *button,
                         gpointer        user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  g_settings_set_boolean (self->screensaver_settings, "lock-enabled",
                          gtk_check_button_get_active (button));
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
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *side = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *choose = gtk_button_new_with_label ("Choose…");
  GtkWidget *group;

  self->wallpaper_preview = preview;
  gtk_widget_set_size_request (preview, 520, 360);
  gtk_widget_set_hexpand (preview, TRUE);
  gtk_widget_set_vexpand (preview, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_draw_wallpaper, self, NULL);
  gtk_box_append (GTK_BOX (page), preview);
  gtk_widget_set_size_request (side, 290, -1);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), side);
  gtk_widget_set_size_request (scroll, 300, -1);
  gtk_box_append (GTK_BOX (page), scroll);

  gtk_box_append (GTK_BOX (side), scenery_section_label ("Ooze Aqua"));
  self->wallpaper_tiles = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->wallpaper_tiles),
                                   GTK_SELECTION_NONE);
  gtk_flow_box_append (GTK_FLOW_BOX (self->wallpaper_tiles),
                       scenery_make_tile (self, "Light", "aqua", NULL));
  gtk_flow_box_append (GTK_FLOW_BOX (self->wallpaper_tiles),
                       scenery_make_tile (self, "Dark", "aqua-dark", NULL));
  gtk_box_append (GTK_BOX (side), self->wallpaper_tiles);

  gtk_box_append (GTK_BOX (side), scenery_section_label ("Ubuntu"));
  self->ubuntu_tiles = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->ubuntu_tiles),
                                   GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (side), self->ubuntu_tiles);
  group = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append (GTK_BOX (group), gtk_label_new ("Your images"));
  gtk_box_append (GTK_BOX (group), choose);
  gtk_box_append (GTK_BOX (side), group);

  g_signal_connect (choose, "clicked", G_CALLBACK (on_choose_clicked), self);
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
  GtkWidget *button = gtk_button_new_with_label (label);

  g_object_set_data (G_OBJECT (button), "mode", (gpointer) mode);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (on_screensaver_mode_clicked), self);
  return button;
}

static GtkWidget *
scenery_build_screensaver_page (OozeSceneryWindow *self)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 18);
  GtkWidget *preview = gtk_drawing_area_new ();
  GtkWidget *side = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *modes = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *label;

  self->screensaver_preview = preview;
  gtk_widget_set_size_request (preview, 520, 360);
  gtk_widget_set_hexpand (preview, TRUE);
  gtk_widget_set_vexpand (preview, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (preview),
                                  scenery_draw_screensaver, self, NULL);
  gtk_box_append (GTK_BOX (page), preview);
  gtk_widget_set_size_request (side, 290, -1);
  gtk_box_append (GTK_BOX (page), side);
  gtk_box_append (GTK_BOX (side), scenery_section_label ("Screensaver"));
  self->screensaver_mode_tiles = modes;
  gtk_box_append (GTK_BOX (modes), scenery_mode_tile (self, "Ooze Flow", "flow"));
  gtk_box_append (GTK_BOX (modes), scenery_mode_tile (self, "None", "none"));
  gtk_box_append (GTK_BOX (side), modes);

  label = scenery_section_label ("Timings");
  gtk_box_append (GTK_BOX (side), label);
  self->start_after = gtk_spin_button_new_with_range (0, 86400, 1);
  gtk_spin_button_set_value (
    GTK_SPIN_BUTTON (self->start_after),
    g_settings_get_uint (self->session_settings, "idle-delay"));
  gtk_box_append (GTK_BOX (side), gtk_label_new ("Start after (seconds)"));
  gtk_box_append (GTK_BOX (side), self->start_after);
  self->lock_enabled = gtk_check_button_new_with_label ("Lock screen");
  gtk_check_button_set_active (
    GTK_CHECK_BUTTON (self->lock_enabled),
    g_settings_get_boolean (self->screensaver_settings, "lock-enabled"));
  gtk_box_append (GTK_BOX (side), self->lock_enabled);
  self->lock_after = gtk_spin_button_new_with_range (0, 86400, 1);
  gtk_spin_button_set_value (
    GTK_SPIN_BUTTON (self->lock_after),
    g_settings_get_uint (self->screensaver_settings, "lock-delay"));
  gtk_box_append (GTK_BOX (side), gtk_label_new ("Lock after (seconds)"));
  gtk_box_append (GTK_BOX (side), self->lock_after);
  g_signal_connect (self->start_after, "value-changed",
                    G_CALLBACK (on_start_after_changed), self);
  g_signal_connect (self->lock_enabled, "toggled",
                    G_CALLBACK (on_lock_enabled_changed), self);
  g_signal_connect (self->lock_after, "value-changed",
                    G_CALLBACK (on_lock_after_changed), self);
  return page;
}

static void
on_tab_clicked (GtkButton *button,
                gpointer   user_data)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (user_data);
  const char *page = g_object_get_data (G_OBJECT (button), "page");

  self->page = g_strcmp0 (page, "screensaver") == 0
    ? SCENERY_PAGE_SCREENSAVER : SCENERY_PAGE_WALLPAPER;
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), page);
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
  g_clear_pointer (&self->selected_uri, g_free);
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
  GtkWidget *shell;
  GtkWidget *toolbar;
  GtkWidget *tabs;
  GMenu *help;
  GSimpleAction *about;

  G_OBJECT_CLASS (ooze_scenery_window_parent_class)->constructed (object);
  ooze_toolbar_ensure_css ();
  self->background_settings = g_settings_new ("org.gnome.desktop.background");
  self->scenery_settings = g_settings_new ("org.ooze.scenery");
  self->session_settings = g_settings_new ("org.gnome.desktop.session");
  self->screensaver_settings = g_settings_new ("org.gnome.desktop.screensaver");
  self->page = SCENERY_PAGE_WALLPAPER;

  gtk_window_set_default_size (GTK_WINDOW (self), 860, 560);
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Scenery");
  ooze_application_window_set_title (OOZE_APPLICATION_WINDOW (self),
                                      "Ooze Scenery");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  toolbar = ooze_toolbar_new ();
  tabs = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  self->tab_wallpaper = gtk_button_new_with_label ("Wallpaper");
  self->tab_screensaver = gtk_button_new_with_label ("Screensaver");
  g_object_set_data (G_OBJECT (self->tab_wallpaper), "page", "wallpaper");
  g_object_set_data (G_OBJECT (self->tab_screensaver), "page", "screensaver");
  gtk_box_append (GTK_BOX (tabs), self->tab_wallpaper);
  gtk_box_append (GTK_BOX (tabs), self->tab_screensaver);
  g_signal_connect (self->tab_wallpaper, "clicked",
                    G_CALLBACK (on_tab_clicked), self);
  g_signal_connect (self->tab_screensaver, "clicked",
                    G_CALLBACK (on_tab_clicked), self);
  gtk_box_append (GTK_BOX (toolbar), tabs);
  gtk_box_append (GTK_BOX (shell), toolbar);

  self->stack = gtk_stack_new ();
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_stack_add_named (GTK_STACK (self->stack),
                       scenery_build_wallpaper_page (self), "wallpaper");
  gtk_stack_add_named (GTK_STACK (self->stack),
                       scenery_build_screensaver_page (self), "screensaver");
  gtk_box_append (GTK_BOX (shell), self->stack);
  ooze_application_window_set_content (OOZE_APPLICATION_WINDOW (self), shell);
  scenery_add_ubuntu_images (self);
  scenery_refresh_selection (self);
  scenery_refresh_mode_selection (self);
  scenery_refresh_preview (self);

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
