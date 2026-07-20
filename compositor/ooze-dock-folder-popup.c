#include "ooze-dock-folder-popup.h"

#include "ooze-aqua-draw.h"
#include "ooze-theme.h"
#include "ooze-icon-lookup.h"

#include <cairo/cairo.h>
#include <gio/gio.h>
#include <math.h>

#define FP_COLS            4
#define FP_CELL_W          92.0f
#define FP_CELL_H          84.0f
#define FP_ICON            48
#define FP_PAD             10.0f
#define FP_CORNER          10.0f
#define FP_MAX_ENTRIES     60
#define FP_MAX_ROWS_VIS    4
#define FP_LABEL_MAX_W     84
#define FP_OPEN_MS         140
#define FP_CLOSE_MS        100
#define FP_SLIDE_PX        10.0f

typedef struct _OozeDockFolderPopup
{
  MetaContext *context;
  ClutterActor *stage;
  ClutterActor *anchor;
  ClutterActor *backdrop;
  ClutterActor *popup;
  ClutterActor *background;
  ClutterActor *grid;      /* holds the cell actors, scrolled in y */
  ClutterActor *highlight;
  OozeDockFolderOpenDirFunc open_dir;
  gpointer open_dir_data;
  char *path;
  gboolean open;
  gboolean closing;
  guint close_timeout;
  gfloat popup_w;
  gfloat content_height;
  gfloat visible_h;
  gfloat scroll_y;
} OozeDockFolderPopup;

static void ooze_dock_folder_popup_do_close (OozeDockFolderPopup *popup);

/* ── drawing ─────────────────────────────────────────────────────────── */

static ClutterContent *
folder_popup_background (ClutterActor *ref_actor,
                         int           width,
                         int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.45);
  cairo_rectangle (cr, 3.0, 4.0, width - 2.0, height - 1.0);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.22);
  cairo_rectangle (cr, 1.0, 2.0, width - 1.0, height - 1.0);
  cairo_fill (cr);

  cairo_new_sub_path (cr);
  cairo_arc (cr, FP_CORNER, FP_CORNER, FP_CORNER, G_PI, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - FP_CORNER, FP_CORNER, FP_CORNER,
             3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, width - FP_CORNER, height - FP_CORNER, FP_CORNER,
             0.0, G_PI / 2.0);
  cairo_arc (cr, FP_CORNER, height - FP_CORNER, FP_CORNER,
             G_PI / 2.0, G_PI);
  cairo_close_path (cr);
  cairo_clip (cr);

  for (int y = 0; y < height; y += 2)
    {
      cairo_set_source_rgb (cr,
                            palette->pinstripe_base_r,
                            palette->pinstripe_base_g,
                            palette->pinstripe_base_b);
      cairo_rectangle (cr, 0, y, width, 1);
      cairo_fill (cr);

      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, palette->pinstripe_highlight_a);
      cairo_rectangle (cr, 0, y + 1, width, 1);
      cairo_fill (cr);
    }

  cairo_reset_clip (cr);
  cairo_new_sub_path (cr);
  cairo_arc (cr, FP_CORNER, FP_CORNER, FP_CORNER, G_PI, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - FP_CORNER, FP_CORNER, FP_CORNER,
             3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, width - FP_CORNER, height - FP_CORNER, FP_CORNER,
             0.0, G_PI / 2.0);
  cairo_arc (cr, FP_CORNER, height - FP_CORNER, FP_CORNER,
             G_PI / 2.0, G_PI);
  cairo_close_path (cr);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.55);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  cairo_destroy (cr);
  content = ooze_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterContent *
folder_cell_highlight (ClutterActor *ref_actor,
                       int           width,
                       int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  const double r = 8.0;

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  cairo_new_sub_path (cr);
  cairo_arc (cr, r, r, r, G_PI, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - r, r, r, 3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, width - r, height - r, r, 0.0, G_PI / 2.0);
  cairo_arc (cr, r, height - r, r, G_PI / 2.0, G_PI);
  cairo_close_path (cr);
  cairo_set_source_rgba (cr, 0.30, 0.52, 0.96, 0.30);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = ooze_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

/* ── entries ─────────────────────────────────────────────────────────── */

typedef struct
{
  char *display_name;
  char *path;
  gboolean is_dir;
  GIcon *icon; /* owned */
} FolderEntry;

static void
folder_entry_free (gpointer data)
{
  FolderEntry *e = data;

  g_free (e->display_name);
  g_free (e->path);
  g_clear_object (&e->icon);
  g_free (e);
}

static gint
folder_entry_cmp (gconstpointer a, gconstpointer b)
{
  const FolderEntry *ea = *(FolderEntry * const *) a;
  const FolderEntry *eb = *(FolderEntry * const *) b;

  if (ea->is_dir != eb->is_dir)
    return ea->is_dir ? -1 : 1;
  return g_utf8_collate (ea->display_name, eb->display_name);
}

static GPtrArray *
folder_load_entries (const char *path)
{
  g_autoptr (GFile) dir = g_file_new_for_path (path);
  g_autoptr (GFileEnumerator) en = NULL;
  GPtrArray *out = g_ptr_array_new_with_free_func (folder_entry_free);

  en = g_file_enumerate_children (dir,
                                  G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                  G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                  G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                  G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                  G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                  G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (!en)
    return out;

  while (TRUE)
    {
      GFileInfo *info = g_file_enumerator_next_file (en, NULL, NULL);
      FolderEntry *e;
      const char *name;
      const char *display;

      if (!info)
        break;
      if (g_file_info_get_is_hidden (info))
        {
          g_object_unref (info);
          continue;
        }

      name = g_file_info_get_name (info);
      display = g_file_info_get_display_name (info);

      e = g_new0 (FolderEntry, 1);
      e->display_name = g_strdup (display ? display : name);
      e->path = g_build_filename (path, name, NULL);
      e->is_dir =
        g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
      e->icon = g_file_info_get_icon (info);
      if (e->icon)
        g_object_ref (e->icon);
      g_ptr_array_add (out, e);

      g_object_unref (info);
    }

  g_ptr_array_sort (out, folder_entry_cmp);
  if (out->len > FP_MAX_ENTRIES)
    g_ptr_array_set_size (out, FP_MAX_ENTRIES);
  return out;
}

/* ── cell interaction ────────────────────────────────────────────────── */

static void
folder_popup_set_hover (OozeDockFolderPopup *popup,
                        ClutterActor        *cell,
                        gboolean             hovered)
{
  if (!popup->highlight)
    return;

  if (!hovered || !cell)
    {
      clutter_actor_hide (popup->highlight);
      return;
    }

  clutter_actor_set_position (popup->highlight,
                              clutter_actor_get_x (cell) + 4.0f,
                              clutter_actor_get_y (cell) - popup->scroll_y + 2.0f);
  clutter_actor_show (popup->highlight);
}

static gboolean
folder_cell_enter (ClutterActor *actor,
                   ClutterEvent *event G_GNUC_UNUSED,
                   OozeDockFolderPopup *popup)
{
  folder_popup_set_hover (popup, actor, TRUE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
folder_cell_leave (ClutterActor *actor G_GNUC_UNUSED,
                   ClutterEvent *event G_GNUC_UNUSED,
                   OozeDockFolderPopup *popup)
{
  folder_popup_set_hover (popup, NULL, FALSE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
folder_cell_pressed (ClutterActor *actor,
                     ClutterEvent *event,
                     OozeDockFolderPopup *popup)
{
  const char *path;
  gboolean is_dir;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  path = g_object_get_data (G_OBJECT (actor), "entry-path");
  is_dir = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "entry-dir"));

  ooze_dock_folder_popup_do_close (popup);

  if (!path)
    return CLUTTER_EVENT_STOP;

  if (is_dir && popup->open_dir)
    {
      popup->open_dir (path, popup->open_dir_data);
    }
  else
    {
      g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
      if (uri)
        g_app_info_launch_default_for_uri (uri, NULL, NULL);
    }

  return CLUTTER_EVENT_STOP;
}

/* ── grid build ──────────────────────────────────────────────────────── */

static void
folder_add_cell (OozeDockFolderPopup *popup,
                 FolderEntry         *entry,
                 int                  col,
                 int                  row)
{
  ClutterActor *cell;
  ClutterActor *icon;
  ClutterActor *label;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (ClutterContent) icon_content = NULL;
  g_autoptr (ClutterContent) label_content = NULL;
  MetaDisplay *display = meta_context_get_display (popup->context);
  int tex = ooze_aqua_icon_texture_size (display, FP_ICON);
  int lw = 1;
  int lh = 1;
  const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);

  cell = clutter_actor_new ();
  clutter_actor_set_reactive (cell, TRUE);
  clutter_actor_set_size (cell, FP_CELL_W, FP_CELL_H);
  clutter_actor_set_position (cell,
                              FP_PAD + col * FP_CELL_W,
                              FP_PAD + row * FP_CELL_H);
  g_object_set_data_full (G_OBJECT (cell), "entry-path",
                          g_strdup (entry->path), g_free);
  g_object_set_data (G_OBJECT (cell), "entry-dir",
                     GINT_TO_POINTER (entry->is_dir ? 1 : 0));

  icon = clutter_actor_new ();
  if (entry->icon)
    pixbuf = ooze_icon_lookup_from_gicon (entry->icon, tex);
  if (!pixbuf)
    pixbuf = ooze_icon_lookup_load (entry->is_dir ? "folder"
                                                  : "text-x-generic", tex);
  if (pixbuf)
    icon_content = ooze_aqua_content_from_pixbuf (icon, pixbuf);
  if (icon_content)
    ooze_aqua_actor_set_scaled_content (icon,
                                        g_steal_pointer (&icon_content),
                                        FP_ICON, FP_ICON, tex, tex);
  clutter_actor_set_size (icon, (gfloat) FP_ICON, (gfloat) FP_ICON);
  clutter_actor_set_position (icon, (FP_CELL_W - FP_ICON) / 2.0f, 6.0f);
  clutter_actor_add_child (cell, icon);
  clutter_actor_show (icon);

  label_content =
    ooze_aqua_text_content_ellipsize (cell, "Sans 10", entry->display_name,
                                      (gfloat) palette->menu_text_r,
                                      (gfloat) palette->menu_text_g,
                                      (gfloat) palette->menu_text_b,
                                      FP_LABEL_MAX_W, &lw, &lh);
  label = clutter_actor_new ();
  if (label_content)
    ooze_aqua_actor_set_content (label, g_steal_pointer (&label_content),
                                 lw, lh);
  clutter_actor_set_position (label,
                              (FP_CELL_W - lw) / 2.0f,
                              6.0f + FP_ICON + 4.0f);
  clutter_actor_add_child (cell, label);
  clutter_actor_show (label);

  g_signal_connect (cell, "enter-event",
                    G_CALLBACK (folder_cell_enter), popup);
  g_signal_connect (cell, "leave-event",
                    G_CALLBACK (folder_cell_leave), popup);
  g_signal_connect (cell, "button-press-event",
                    G_CALLBACK (folder_cell_pressed), popup);

  clutter_actor_add_child (popup->grid, cell);
  clutter_actor_show (cell);
}

static void
folder_build_grid (OozeDockFolderPopup *popup)
{
  g_autoptr (GPtrArray) entries = NULL;
  guint i;
  int rows;

  clutter_actor_destroy_all_children (popup->grid);

  entries = folder_load_entries (popup->path);

  if (entries->len == 0)
    {
      ClutterActor *label = clutter_actor_new ();
      g_autoptr (ClutterContent) c = NULL;
      const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);
      int lw = 1;
      int lh = 1;

      c = ooze_aqua_text_content_ellipsize (popup->grid, "Sans 11",
                                            "Empty folder",
                                            (gfloat) palette->menu_text_r * 0.6f,
                                            (gfloat) palette->menu_text_g * 0.6f,
                                            (gfloat) palette->menu_text_b * 0.6f,
                                            FP_COLS * (int) FP_CELL_W,
                                            &lw, &lh);
      if (c)
        ooze_aqua_actor_set_content (label, g_steal_pointer (&c), lw, lh);
      clutter_actor_set_position (label, FP_PAD, FP_PAD + 8.0f);
      clutter_actor_add_child (popup->grid, label);
      clutter_actor_show (label);

      popup->popup_w = FP_COLS * FP_CELL_W + 2.0f * FP_PAD;
      popup->content_height = FP_PAD * 2.0f + FP_CELL_H / 2.0f;
      return;
    }

  for (i = 0; i < entries->len; i++)
    folder_add_cell (popup, entries->pdata[i],
                     (int) (i % FP_COLS), (int) (i / FP_COLS));

  rows = (int) ((entries->len + FP_COLS - 1) / FP_COLS);
  popup->popup_w = FP_COLS * FP_CELL_W + 2.0f * FP_PAD;
  popup->content_height = 2.0f * FP_PAD + rows * FP_CELL_H;
}

/* ── positioning ─────────────────────────────────────────────────────── */

static void
folder_popup_resize (OozeDockFolderPopup *popup,
                     gfloat               height)
{
  g_autoptr (ClutterContent) bg = NULL;

  clutter_actor_set_size (popup->popup, popup->popup_w, height);
  clutter_actor_set_size (popup->grid, popup->popup_w, height);

  g_clear_pointer (&popup->background, clutter_actor_destroy);
  bg = folder_popup_background (popup->popup, (int) popup->popup_w,
                               (int) height);
  popup->background = clutter_actor_new ();
  if (bg)
    ooze_aqua_actor_set_content (popup->background, g_steal_pointer (&bg),
                                 (int) popup->popup_w, (int) height);
  clutter_actor_add_child (popup->popup, popup->background);
  clutter_actor_set_child_below_sibling (popup->popup, popup->background,
                                         popup->highlight);
  clutter_actor_show (popup->background);
}

static void
folder_popup_reposition (OozeDockFolderPopup *popup)
{
  gfloat anchor_x = 0.0f;
  gfloat anchor_y = 0.0f;
  gfloat anchor_w;
  gfloat stage_w;
  gfloat stage_h;
  gfloat max_vis;
  gfloat x;
  gfloat y;
  const gfloat margin = 8.0f;

  if (!popup->anchor || !popup->stage)
    return;

  clutter_actor_get_transformed_position (popup->anchor, &anchor_x, &anchor_y);
  if (!isfinite (anchor_x) || !isfinite (anchor_y))
    {
      clutter_actor_get_position (popup->anchor, &anchor_x, &anchor_y);
    }
  anchor_w = clutter_actor_get_width (popup->anchor);
  if (!isfinite (anchor_w) || anchor_w < 1.0f)
    anchor_w = 48.0f;

  stage_w = clutter_actor_get_width (popup->stage);
  stage_h = clutter_actor_get_height (popup->stage);
  if (!isfinite (stage_w) || stage_w < 1.0f)
    stage_w = 1280.0f;
  if (!isfinite (stage_h) || stage_h < 1.0f)
    stage_h = 800.0f;

  max_vis = 2.0f * FP_PAD + FP_MAX_ROWS_VIS * FP_CELL_H;
  popup->visible_h = MIN (popup->content_height, max_vis);
  if (popup->visible_h < 1.0f)
    popup->visible_h = FP_CELL_H;

  /* Sit above the dock icon, horizontally centered on it. */
  x = anchor_x + anchor_w / 2.0f - popup->popup_w / 2.0f;
  if (x + popup->popup_w > stage_w - margin)
    x = stage_w - popup->popup_w - margin;
  if (x < margin)
    x = margin;

  y = anchor_y - 4.0f - popup->visible_h;
  if (y < margin)
    y = margin;

  popup->scroll_y = 0.0f;
  clutter_actor_set_position (popup->grid, 0.0f, 0.0f);
  folder_popup_resize (popup, popup->visible_h);
  clutter_actor_set_clip_to_allocation (popup->popup,
                                        popup->content_height >
                                          popup->visible_h + 0.5f);
  clutter_actor_set_position (popup->popup, x, y);
}

static gboolean
folder_popup_on_scroll (ClutterActor *actor G_GNUC_UNUSED,
                        ClutterEvent *event,
                        OozeDockFolderPopup *popup)
{
  gfloat max_scroll;
  gdouble dx = 0.0, dy = 0.0;
  ClutterScrollDirection dir;

  if (!popup->open)
    return CLUTTER_EVENT_PROPAGATE;

  max_scroll = popup->content_height - popup->visible_h;
  if (max_scroll <= 0.5f)
    return CLUTTER_EVENT_PROPAGATE;

  dir = clutter_event_get_scroll_direction (event);
  if (dir == CLUTTER_SCROLL_SMOOTH)
    clutter_event_get_scroll_delta (event, &dx, &dy);
  else if (dir == CLUTTER_SCROLL_UP)
    dy = -1.0;
  else if (dir == CLUTTER_SCROLL_DOWN)
    dy = 1.0;
  else
    return CLUTTER_EVENT_PROPAGATE;

  popup->scroll_y += (gfloat) dy * FP_CELL_H / 2.0f;
  if (popup->scroll_y < 0.0f)
    popup->scroll_y = 0.0f;
  if (popup->scroll_y > max_scroll)
    popup->scroll_y = max_scroll;

  clutter_actor_set_position (popup->grid, 0.0f, -popup->scroll_y);
  clutter_actor_hide (popup->highlight);
  return CLUTTER_EVENT_STOP;
}

/* ── open / close ────────────────────────────────────────────────────── */

static gboolean
folder_popup_backdrop_pressed (ClutterActor *actor G_GNUC_UNUSED,
                               ClutterEvent *event G_GNUC_UNUSED,
                               OozeDockFolderPopup *popup)
{
  ooze_dock_folder_popup_do_close (popup);
  return CLUTTER_EVENT_STOP;
}

static void
folder_popup_ensure_actors (OozeDockFolderPopup *popup)
{
  if (popup->popup)
    return;

  popup->backdrop = clutter_actor_new ();
  clutter_actor_set_reactive (popup->backdrop, TRUE);
  clutter_actor_set_opacity (popup->backdrop, 0);
  clutter_actor_add_child (popup->stage, popup->backdrop);
  clutter_actor_hide (popup->backdrop);
  g_signal_connect (popup->backdrop, "button-press-event",
                    G_CALLBACK (folder_popup_backdrop_pressed), popup);
  g_signal_connect (popup->backdrop, "scroll-event",
                    G_CALLBACK (folder_popup_on_scroll), popup);

  popup->popup = clutter_actor_new ();
  clutter_actor_set_reactive (popup->popup, TRUE);
  clutter_actor_add_child (popup->stage, popup->popup);
  clutter_actor_hide (popup->popup);
  g_signal_connect (popup->popup, "scroll-event",
                    G_CALLBACK (folder_popup_on_scroll), popup);

  popup->highlight = clutter_actor_new ();
  {
    g_autoptr (ClutterContent) hl =
      folder_cell_highlight (popup->popup, (int) (FP_CELL_W - 8.0f),
                             (int) (FP_CELL_H - 4.0f));
    if (hl)
      ooze_aqua_actor_set_content (popup->highlight, g_steal_pointer (&hl),
                                   (int) (FP_CELL_W - 8.0f),
                                   (int) (FP_CELL_H - 4.0f));
  }
  clutter_actor_set_size (popup->highlight, FP_CELL_W - 8.0f, FP_CELL_H - 4.0f);
  clutter_actor_add_child (popup->popup, popup->highlight);
  clutter_actor_hide (popup->highlight);

  popup->grid = clutter_actor_new ();
  clutter_actor_add_child (popup->popup, popup->grid);
  clutter_actor_show (popup->grid);
}

static gboolean
folder_popup_finish_close (gpointer user_data)
{
  OozeDockFolderPopup *popup = user_data;

  popup->close_timeout = 0;
  popup->closing = FALSE;
  if (popup->popup)
    {
      clutter_actor_remove_all_transitions (popup->popup);
      clutter_actor_hide (popup->popup);
      clutter_actor_set_opacity (popup->popup, 255);
    }
  if (popup->backdrop)
    clutter_actor_hide (popup->backdrop);
  return G_SOURCE_REMOVE;
}

static void
ooze_dock_folder_popup_do_close (OozeDockFolderPopup *popup)
{
  if (!popup->open && !popup->closing)
    return;

  popup->open = FALSE;
  popup->closing = TRUE;
  folder_popup_set_hover (popup, NULL, FALSE);

  if (popup->backdrop)
    clutter_actor_hide (popup->backdrop);

  if (popup->popup)
    {
      clutter_actor_remove_all_transitions (popup->popup);
      clutter_actor_set_easing_duration (popup->popup, FP_CLOSE_MS);
      clutter_actor_set_easing_mode (popup->popup, CLUTTER_EASE_OUT_CUBIC);
      clutter_actor_set_opacity (popup->popup, 0);
      clutter_actor_set_translation (popup->popup, 0.0f, FP_SLIDE_PX, 0.0f);
    }

  if (popup->close_timeout)
    g_source_remove (popup->close_timeout);
  popup->close_timeout = g_timeout_add (FP_CLOSE_MS + 20,
                                        folder_popup_finish_close, popup);
}

static void
folder_popup_open (OozeDockFolderPopup *popup)
{
  gfloat stage_w;
  gfloat stage_h;

  folder_popup_ensure_actors (popup);

  if (popup->close_timeout)
    {
      g_source_remove (popup->close_timeout);
      folder_popup_finish_close (popup);
    }

  folder_build_grid (popup);
  folder_popup_reposition (popup);

  stage_w = clutter_actor_get_width (popup->stage);
  stage_h = clutter_actor_get_height (popup->stage);
  clutter_actor_set_position (popup->backdrop, 0.0f, 0.0f);
  clutter_actor_set_size (popup->backdrop, stage_w, stage_h);
  clutter_actor_set_child_above_sibling (popup->stage, popup->backdrop, NULL);
  clutter_actor_set_child_above_sibling (popup->stage, popup->popup, NULL);
  clutter_actor_show (popup->backdrop);

  clutter_actor_remove_all_transitions (popup->popup);
  clutter_actor_set_opacity (popup->popup, 0);
  clutter_actor_set_translation (popup->popup, 0.0f, FP_SLIDE_PX, 0.0f);
  clutter_actor_show (popup->popup);
  clutter_actor_set_easing_duration (popup->popup, FP_OPEN_MS);
  clutter_actor_set_easing_mode (popup->popup, CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_opacity (popup->popup, 255);
  clutter_actor_set_translation (popup->popup, 0.0f, 0.0f, 0.0f);

  popup->open = TRUE;
  popup->closing = FALSE;
}

/* ── public API ──────────────────────────────────────────────────────── */

OozeDockFolderPopup *
ooze_dock_folder_popup_new (MetaContext  *context,
                            ClutterActor *stage)
{
  OozeDockFolderPopup *popup = g_new0 (OozeDockFolderPopup, 1);

  popup->context = context;
  popup->stage = stage;
  return popup;
}

void
ooze_dock_folder_popup_set_open_dir_func (OozeDockFolderPopup       *popup,
                                          OozeDockFolderOpenDirFunc  func,
                                          gpointer                   user_data)
{
  popup->open_dir = func;
  popup->open_dir_data = user_data;
}

void
ooze_dock_folder_popup_toggle (OozeDockFolderPopup *popup,
                               ClutterActor        *anchor,
                               const char          *path)
{
  gboolean same_anchor;

  if (!popup || !anchor || !path)
    return;

  same_anchor = popup->open && popup->anchor == anchor &&
                g_strcmp0 (popup->path, path) == 0;
  if (same_anchor)
    {
      ooze_dock_folder_popup_do_close (popup);
      return;
    }

  popup->anchor = anchor;
  g_clear_pointer (&popup->path, g_free);
  popup->path = g_strdup (path);
  folder_popup_open (popup);
}

void
ooze_dock_folder_popup_close (OozeDockFolderPopup *popup)
{
  if (popup)
    ooze_dock_folder_popup_do_close (popup);
}

gboolean
ooze_dock_folder_popup_is_open (OozeDockFolderPopup *popup)
{
  return popup && popup->open;
}

void
ooze_dock_folder_popup_destroy (OozeDockFolderPopup *popup)
{
  if (!popup)
    return;

  if (popup->close_timeout)
    g_source_remove (popup->close_timeout);
  g_clear_pointer (&popup->backdrop, clutter_actor_destroy);
  g_clear_pointer (&popup->popup, clutter_actor_destroy);
  g_free (popup->path);
  g_free (popup);
}
