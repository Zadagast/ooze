#include "ooze-scenery-window.h"

#include "ooze-about.h"
#include "ooze-action-bar.h"
#include "ooze-button.h"
#include "ooze-shared-appmenu.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "../ooze-kit/ooze-theme.h"
#include "ooze-toolbar.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>

typedef enum
{
  SCENERY_PAGE_WALLPAPER,
  SCENERY_PAGE_SCREENLOCK,
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
  GSettings *session_settings;
  GSettings *screensaver_settings;
  GtkWidget *stack;
  GtkWidget *wallpaper_preview;
  GtkWidget *wallpaper_tiles;
  GtkWidget *ubuntu_tiles;
  GtkWidget *start_after;
  GtkWidget *lock_enabled;
  GtkWidget *lock_after;
  GtkWidget *nav_wallpaper;
  GtkWidget *nav_screenlock;
  GtkWidget *custom_tiles;
  GtkWidget *action_bar;
  GtkWidget *choose_button;
  GdkPixbuf *selected_pixbuf;
  /* Staged (uncommitted) wallpaper edit — written to GSettings on Apply.
   * Screen Lock settings are instant-apply via g_settings_bind. */
  char *pending_wallpaper;   /* "aqua", "aqua-dark", or an absolute path */
  gboolean dirty;
  gboolean loading;
  SceneryPage page;
};

G_DEFINE_FINAL_TYPE (OozeSceneryWindow, ooze_scenery_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void scenery_refresh_selection (OozeSceneryWindow *self);
static void scenery_refresh_preview (OozeSceneryWindow *self);
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
    ".scenery-unit { color: alpha(currentColor, 0.55); }"
    ".scenery-rows {"
    "  background: alpha(@card_bg_color, 0.72);"
    "  border-radius: 8px;"
    "  box-shadow: inset 0 0 0 1px rgba(0,0,0,0.10);"
    "  padding: 0;"
    "}"
    ".scenery-rows > row {"
    "  padding: 10px 14px;"
    "  background: none;"
    "  border-radius: 0;"
    "}"
    ".scenery-rows > row:not(:last-child) {"
    "  border-bottom: 1px solid alpha(currentColor, 0.10);"
    "}"
    ".scenery-rows > row:first-child { border-radius: 8px 8px 0 0; }"
    ".scenery-rows > row:last-child { border-radius: 0 0 8px 8px; }"
    ".scenery-hint {"
    "  color: alpha(currentColor, 0.55);"
    "  font-size: 0.88em;"
    "}");
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
}

static void
scenery_reload (OozeSceneryWindow *self)
{
  self->loading = TRUE;
  scenery_load_pending (self);
  scenery_load_pending_pixbuf (self);
  scenery_refresh_selection (self);
  scenery_refresh_preview (self);
  self->loading = FALSE;
  scenery_set_dirty (self, FALSE);
}

static void
scenery_apply (OozeSceneryWindow *self)
{
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

/* Fixed duration choices (GNOME-style dropdowns) — instant apply. */
typedef struct
{
  const char *label;
  guint seconds;
} SceneryDuration;

static const SceneryDuration scenery_fade_choices[] = {
  { "Never", 0 },
  { "1 minute", 60 },
  { "2 minutes", 120 },
  { "5 minutes", 300 },
  { "10 minutes", 600 },
  { "15 minutes", 900 },
  { "30 minutes", 1800 },
  { "1 hour", 3600 },
};

static const SceneryDuration scenery_lock_choices[] = {
  { "Immediately", 0 },
  { "30 seconds", 30 },
  { "1 minute", 60 },
  { "2 minutes", 120 },
  { "5 minutes", 300 },
  { "30 minutes", 1800 },
  { "1 hour", 3600 },
};

typedef struct
{
  GSettings *settings;
  const char *key;
  const SceneryDuration *choices;
  guint n_choices;
  gboolean syncing;
} SceneryDurationBinding;

static void
scenery_duration_sync_selected (SceneryDurationBinding *binding,
                                GtkDropDown            *dropdown)
{
  guint value = g_settings_get_uint (binding->settings, binding->key);
  guint best = 0;
  guint i;

  /* Pick the exact match, or the nearest choice below the value. */
  for (i = 0; i < binding->n_choices; i++)
    {
      if (binding->choices[i].seconds == value)
        {
          best = i;
          break;
        }
      if (binding->choices[i].seconds < value)
        best = i;
    }

  binding->syncing = TRUE;
  gtk_drop_down_set_selected (dropdown, best);
  binding->syncing = FALSE;
}

static void
on_duration_selected (GtkDropDown *dropdown,
                      GParamSpec  *pspec G_GNUC_UNUSED,
                      gpointer     user_data)
{
  SceneryDurationBinding *binding = user_data;
  guint selected = gtk_drop_down_get_selected (dropdown);

  if (binding->syncing || selected >= binding->n_choices)
    return;

  g_settings_set_uint (binding->settings, binding->key,
                       binding->choices[selected].seconds);
}

static void
on_duration_settings_changed (GSettings  *settings G_GNUC_UNUSED,
                              const char *key G_GNUC_UNUSED,
                              gpointer    user_data)
{
  GtkDropDown *dropdown = GTK_DROP_DOWN (user_data);
  SceneryDurationBinding *binding =
    g_object_get_data (G_OBJECT (dropdown), "scenery-duration");

  if (binding)
    scenery_duration_sync_selected (binding, dropdown);
}

static void
scenery_duration_binding_free (gpointer data)
{
  SceneryDurationBinding *binding = data;

  g_object_unref (binding->settings);
  g_free (binding);
}

static GtkWidget *
scenery_duration_dropdown_new (GSettings             *settings,
                               const char            *key,
                               const SceneryDuration *choices,
                               guint                  n_choices)
{
  GtkWidget *dropdown;
  GtkStringList *model;
  SceneryDurationBinding *binding;
  g_autofree char *signal_name = NULL;
  guint i;

  model = gtk_string_list_new (NULL);
  for (i = 0; i < n_choices; i++)
    gtk_string_list_append (model, choices[i].label);

  dropdown = gtk_drop_down_new (G_LIST_MODEL (model), NULL);

  binding = g_new0 (SceneryDurationBinding, 1);
  binding->settings = g_object_ref (settings);
  binding->key = key;
  binding->choices = choices;
  binding->n_choices = n_choices;
  g_object_set_data_full (G_OBJECT (dropdown), "scenery-duration",
                          binding, scenery_duration_binding_free);

  scenery_duration_sync_selected (binding, GTK_DROP_DOWN (dropdown));

  g_signal_connect (dropdown, "notify::selected",
                    G_CALLBACK (on_duration_selected), binding);
  signal_name = g_strdup_printf ("changed::%s", key);
  g_signal_connect_object (settings, signal_name,
                           G_CALLBACK (on_duration_settings_changed),
                           dropdown, G_CONNECT_DEFAULT);

  return dropdown;
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
scenery_settings_row (const char *title,
                      const char *subtitle,
                      GtkWidget  *control)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *label = gtk_label_new (title);

  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (GTK_BOX (text), label);
  if (subtitle)
    {
      GtkWidget *sub = gtk_label_new (subtitle);
      gtk_label_set_xalign (GTK_LABEL (sub), 0);
      gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
      gtk_widget_add_css_class (sub, "scenery-hint");
      gtk_box_append (GTK_BOX (text), sub);
    }
  gtk_widget_set_hexpand (text, TRUE);
  gtk_widget_set_valign (text, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (control, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), text);
  gtk_box_append (GTK_BOX (box), control);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  return row;
}

static GtkWidget *
scenery_build_screenlock_page (OozeSceneryWindow *self)
{
  GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  GtkWidget *rows;
  GtkWidget *label;
  GtkWidget *hint;

  gtk_widget_add_css_class (page, "scenery-page");
  gtk_widget_set_hexpand (scroll, TRUE);
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), content);
  gtk_box_append (GTK_BOX (page), scroll);

  gtk_widget_set_halign (content, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (content, 520, -1);

  label = scenery_section_label ("When the computer is idle");
  gtk_box_append (GTK_BOX (content), label);

  rows = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (rows),
                                   GTK_SELECTION_NONE);
  gtk_widget_add_css_class (rows, "scenery-rows");

  self->start_after =
    scenery_duration_dropdown_new (self->session_settings, "idle-delay",
                                   scenery_fade_choices,
                                   G_N_ELEMENTS (scenery_fade_choices));
  gtk_list_box_append (GTK_LIST_BOX (rows),
                       scenery_settings_row ("Fade to black after",
                                             "Choose Never to keep the "
                                             "desktop always on",
                                             self->start_after));

  self->lock_enabled = gtk_switch_new ();
  g_settings_bind (self->screensaver_settings, "lock-enabled",
                   self->lock_enabled, "active",
                   G_SETTINGS_BIND_DEFAULT);
  gtk_list_box_append (GTK_LIST_BOX (rows),
                       scenery_settings_row ("Lock screen",
                                             "Require your password after "
                                             "the fade",
                                             self->lock_enabled));

  self->lock_after =
    scenery_duration_dropdown_new (self->screensaver_settings, "lock-delay",
                                   scenery_lock_choices,
                                   G_N_ELEMENTS (scenery_lock_choices));
  g_object_bind_property (self->lock_enabled, "active",
                          self->lock_after, "sensitive",
                          G_BINDING_SYNC_CREATE);
  gtk_list_box_append (GTK_LIST_BOX (rows),
                       scenery_settings_row ("Lock after the fade",
                                             NULL, self->lock_after));

  gtk_box_append (GTK_BOX (content), rows);

  hint = gtk_label_new ("Changes take effect immediately. Display power "
                        "saving turns the screen off separately.");
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_label_set_xalign (GTK_LABEL (hint), 0);
  gtk_widget_add_css_class (hint, "scenery-hint");
  gtk_box_append (GTK_BOX (content), hint);
  return page;
}

static void
scenery_show_page (OozeSceneryWindow *self,
                   SceneryPage        page)
{
  self->page = page;
  gtk_stack_set_visible_child_name (
    GTK_STACK (self->stack),
    page == SCENERY_PAGE_SCREENLOCK ? "screenlock" : "wallpaper");
  /* Wallpaper is staged Apply/Cancel; Screen Lock applies instantly. */
  if (self->action_bar)
    gtk_widget_set_visible (self->action_bar,
                            page == SCENERY_PAGE_WALLPAPER);
  if (self->choose_button)
    gtk_widget_set_visible (self->choose_button,
                            page == SCENERY_PAGE_WALLPAPER);
  ooze_button_set_toggled (self->nav_wallpaper,
                           page == SCENERY_PAGE_WALLPAPER);
  ooze_button_set_toggled (self->nav_screenlock,
                           page == SCENERY_PAGE_SCREENLOCK);
}

static void
on_nav_wallpaper_clicked (GtkButton *button G_GNUC_UNUSED,
                          gpointer   user_data)
{
  scenery_show_page (OOZE_SCENERY_WINDOW (user_data),
                     SCENERY_PAGE_WALLPAPER);
}

static void
on_nav_screenlock_clicked (GtkButton *button G_GNUC_UNUSED,
                           gpointer   user_data)
{
  scenery_show_page (OOZE_SCENERY_WINDOW (user_data),
                     SCENERY_PAGE_SCREENLOCK);
}

static void
scenery_action_about (GSimpleAction *action G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Scenery",
                      "org.ooze.Scenery",
                      "Wallpaper and screen lock settings for Ooze Desktop.",
                      OOZE_VERSION);
}

static void
ooze_scenery_window_dispose (GObject *object)
{
  OozeSceneryWindow *self = OOZE_SCENERY_WINDOW (object);

  g_clear_object (&self->selected_pixbuf);
  g_clear_pointer (&self->pending_wallpaper, g_free);
  g_clear_object (&self->background_settings);
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
  static const char * const screenlock_icons[] = {
    "system-lock-screen", "preferences-desktop-screensaver",
    "video-display", NULL
  };
  static const char * const choose_icons[] = {
    "folder-open", "document-open", NULL
  };
  GtkWidget *shell;
  GtkWidget *shell_overlay;
  GtkWidget *toolbar;
  GtkWidget *toolbar_group;
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
  self->nav_screenlock =
    ooze_button_new_toolbar (screenlock_icons, "Screen Lock", "Screen Lock");
  g_signal_connect (self->nav_screenlock, "clicked",
                    G_CALLBACK (on_nav_screenlock_clicked), self);
  gtk_box_append (GTK_BOX (toolbar_group), self->nav_screenlock);
  ooze_toolbar_add_separator (toolbar);
  toolbar_group = ooze_toolbar_add_group (toolbar);
  self->choose_button =
    ooze_button_new_toolbar (choose_icons, "Choose…",
                             "Choose an image from your files");
  g_signal_connect (self->choose_button, "clicked",
                    G_CALLBACK (on_choose_clicked), self);
  gtk_box_append (GTK_BOX (toolbar_group), self->choose_button);
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
                       scenery_build_screenlock_page (self), "screenlock");
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
  scenery_refresh_preview (self);
  scenery_set_dirty (self, FALSE);

  g_signal_connect (self->background_settings, "changed::picture-uri",
                    G_CALLBACK (on_setting_changed), self);
  g_signal_connect (self->background_settings, "changed::picture-uri-dark",
                    G_CALLBACK (on_setting_changed), self);
  g_signal_connect (self->background_settings, "changed::picture-options",
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
