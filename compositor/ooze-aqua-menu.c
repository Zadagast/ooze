#include "ooze-aqua-menu.h"

#include "ooze-aqua-draw.h"
#include "ooze-theme.h"

#include <cairo/cairo.h>
#include <math.h>

#define AQUA_MENU_WIDTH       280.0f
#define AQUA_MENU_ROW_HEIGHT  22.0f
#define AQUA_MENU_PAD         4.0f
#define AQUA_MENU_SEPARATOR_H 9.0f
#define AQUA_MENU_CORNER      6.0f
#define AQUA_MENU_LABEL_MAX_W 240
#define AQUA_MENU_OPEN_MS     140
#define AQUA_MENU_CLOSE_MS    100
#define AQUA_MENU_SLIDE_PX    10.0f


typedef struct _OozeAquaMenu
{
  MetaContext *context;
  ClutterActor *stage;
  ClutterActor *anchor;
  ClutterActor *backdrop;
  ClutterActor *popup;
  ClutterActor *background;
  ClutterActor *rows;
  ClutterActor *highlight;
  OozeAquaMenuActionFunc callback;
  gpointer user_data;
  gboolean open;
  gboolean closing;
  guint close_timeout;
  gfloat content_height; /* full unclipped menu height */
  gfloat scroll_y;       /* 0 … max(0, content - visible) */
  gfloat open_x;
  gfloat open_y;
} OozeAquaMenu;

static void ooze_aqua_menu_do_close (OozeAquaMenu *menu);
static void ooze_aqua_menu_open (OozeAquaMenu *menu);

static ClutterContent *
ooze_aqua_menu_popup_background (ClutterActor *ref_actor,
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
  cairo_arc (cr, AQUA_MENU_CORNER, AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             G_PI, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - AQUA_MENU_CORNER, AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, width - AQUA_MENU_CORNER, height - AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             0.0, G_PI / 2.0);
  cairo_arc (cr, AQUA_MENU_CORNER, height - AQUA_MENU_CORNER, AQUA_MENU_CORNER,
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
  cairo_arc (cr, AQUA_MENU_CORNER, AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             G_PI, 3.0 * G_PI / 2.0);
  cairo_arc (cr, width - AQUA_MENU_CORNER, AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, width - AQUA_MENU_CORNER, height - AQUA_MENU_CORNER, AQUA_MENU_CORNER,
             0.0, G_PI / 2.0);
  cairo_arc (cr, AQUA_MENU_CORNER, height - AQUA_MENU_CORNER, AQUA_MENU_CORNER,
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
ooze_aqua_menu_row_highlight (ClutterActor *ref_actor,
                            int           width,
                            int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  cairo_pattern_t *gradient;

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  gradient = cairo_pattern_create_linear (0, 0, 0, height);
  cairo_pattern_add_color_stop_rgba (gradient, 0.0, 0.38, 0.58, 0.98, 1.0);
  cairo_pattern_add_color_stop_rgba (gradient, 0.5, 0.22, 0.45, 0.96, 1.0);
  cairo_pattern_add_color_stop_rgba (gradient, 1.0, 0.12, 0.34, 0.92, 1.0);
  cairo_set_source (cr, gradient);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_fill (cr);
  cairo_pattern_destroy (gradient);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.18);
  cairo_rectangle (cr, 0, 0, width, 1);
  cairo_fill (cr);

  cairo_destroy (cr);
  content = ooze_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterContent *
ooze_aqua_menu_separator_content (ClutterActor *ref_actor,
                                int           width)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);
  int height = (int) AQUA_MENU_SEPARATOR_H;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  cairo_set_source_rgba (cr,
                         palette->menu_text_r,
                         palette->menu_text_g,
                         palette->menu_text_b,
                         0.25);
  cairo_set_line_width (cr, 1.0);
  cairo_move_to (cr, 8.0, height / 2.0 + 0.5);
  cairo_line_to (cr, width - 8.0, height / 2.0 + 0.5);
  cairo_stroke (cr);

  cairo_destroy (cr);
  content = ooze_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterActor *
ooze_aqua_menu_create_label (ClutterActor *ref_actor,
                           const char   *text,
                           gboolean      inverted,
                           gboolean      dimmed)
{
  ClutterActor *label;
  g_autoptr (ClutterContent) content = NULL;
  const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);
  gfloat r;
  gfloat g;
  gfloat b;
  int width = 1;
  int height = 1;

  if (inverted)
    {
      r = 1.0f;
      g = 1.0f;
      b = 1.0f;
    }
  else if (dimmed)
    {
      r = (gfloat) palette->menu_text_r * 0.55f;
      g = (gfloat) palette->menu_text_g * 0.55f;
      b = (gfloat) palette->menu_text_b * 0.55f;
    }
  else
    {
      r = (gfloat) palette->menu_text_r;
      g = (gfloat) palette->menu_text_g;
      b = (gfloat) palette->menu_text_b;
    }

  content = ooze_aqua_text_content_ellipsize (ref_actor,
                                            "Sans 11",
                                            text,
                                            r, g, b,
                                            AQUA_MENU_LABEL_MAX_W,
                                            &width,
                                            &height);
  label = clutter_actor_new ();
  if (content)
    ooze_aqua_actor_set_content (label,
                               g_steal_pointer (&content),
                               width,
                               height);
  clutter_actor_set_position (label, 18.0f, (AQUA_MENU_ROW_HEIGHT - height) / 2.0f);
  return label;
}

static void
ooze_aqua_menu_set_hover (OozeAquaMenu   *menu,
                        ClutterActor *row,
                        gboolean      hovered)
{
  ClutterActor *label;
  const char *text;
  g_autoptr (ClutterContent) highlight = NULL;

  if (!menu->highlight)
    return;

  if (!hovered)
    {
      clutter_actor_hide (menu->highlight);
      if (row)
        {
          label = g_object_get_data (G_OBJECT (row), "menu-label");
          text = g_object_get_data (G_OBJECT (row), "menu-text");
          if (label && text)
            {
              gboolean sensitive;

              sensitive = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row),
                                                              "menu-sensitive"));
              clutter_actor_destroy (label);
              label = ooze_aqua_menu_create_label (menu->popup,
                                                 text,
                                                 FALSE,
                                                 !sensitive);
              g_object_set_data (G_OBJECT (row), "menu-label", label);
              clutter_actor_add_child (row, label);
              clutter_actor_show (label);
            }
        }
      return;
    }

  highlight = ooze_aqua_menu_row_highlight (menu->popup,
                                          (int) AQUA_MENU_WIDTH,
                                          (int) AQUA_MENU_ROW_HEIGHT);
  if (highlight)
    ooze_aqua_actor_set_content (menu->highlight,
                               g_steal_pointer (&highlight),
                               (int) AQUA_MENU_WIDTH,
                               (int) AQUA_MENU_ROW_HEIGHT);
  /* Highlight is a popup sibling of rows; subtract scroll offset. */
  clutter_actor_set_position (menu->highlight,
                              0.0f,
                              clutter_actor_get_y (row) - menu->scroll_y);
  clutter_actor_show (menu->highlight);

  label = g_object_get_data (G_OBJECT (row), "menu-label");
  text = g_object_get_data (G_OBJECT (row), "menu-text");
  if (label && text)
    {
      clutter_actor_destroy (label);
      label = ooze_aqua_menu_create_label (menu->popup, text, TRUE, FALSE);
      g_object_set_data (G_OBJECT (row), "menu-label", label);
      clutter_actor_add_child (row, label);
      clutter_actor_show (label);
    }
}

static gboolean
ooze_aqua_menu_row_enter (ClutterActor *actor,
                        ClutterEvent *event G_GNUC_UNUSED,
                        OozeAquaMenu   *menu)
{
  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-sensitive")))
    return CLUTTER_EVENT_PROPAGATE;

  ooze_aqua_menu_set_hover (menu, actor, TRUE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_aqua_menu_row_leave (ClutterActor *actor,
                        ClutterEvent *event G_GNUC_UNUSED,
                        OozeAquaMenu   *menu)
{
  ooze_aqua_menu_set_hover (menu, actor, FALSE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_aqua_menu_row_pressed (ClutterActor *actor,
                          ClutterEvent *event,
                          OozeAquaMenu   *menu)
{
  int action_id;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-sensitive")))
    return CLUTTER_EVENT_STOP;

  action_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-action"));
  ooze_aqua_menu_do_close (menu);

  if (menu->callback && action_id != 0)
    menu->callback (menu->user_data, action_id);

  return CLUTTER_EVENT_STOP;
}

static void
ooze_aqua_menu_add_row (OozeAquaMenu   *menu,
                      const char  *label,
                      int          action_id,
                      gboolean     sensitive,
                      gfloat       y)
{
  ClutterActor *row;
  ClutterActor *label_actor;

  row = clutter_actor_new ();
  clutter_actor_set_reactive (row, TRUE);
  clutter_actor_set_size (row, AQUA_MENU_WIDTH, AQUA_MENU_ROW_HEIGHT);
  clutter_actor_set_position (row, 0.0f, y);
  g_object_set_data (G_OBJECT (row),
                     "menu-action",
                     GINT_TO_POINTER (action_id));
  g_object_set_data (G_OBJECT (row),
                     "menu-sensitive",
                     GINT_TO_POINTER (sensitive ? 1 : 0));
  g_object_set_data_full (G_OBJECT (row),
                          "menu-text",
                          g_strdup (label),
                          g_free);

  label_actor = ooze_aqua_menu_create_label (menu->popup, label, FALSE, !sensitive);
  g_object_set_data (G_OBJECT (row), "menu-label", label_actor);
  clutter_actor_add_child (row, label_actor);
  clutter_actor_show (label_actor);

  g_signal_connect (row, "enter-event", G_CALLBACK (ooze_aqua_menu_row_enter), menu);
  g_signal_connect (row, "leave-event", G_CALLBACK (ooze_aqua_menu_row_leave), menu);
  g_signal_connect (row, "button-press-event", G_CALLBACK (ooze_aqua_menu_row_pressed), menu);

  clutter_actor_add_child (menu->rows, row);
  clutter_actor_show (row);
}

static void
ooze_aqua_menu_add_separator (OozeAquaMenu *menu,
                            gfloat       y)
{
  ClutterActor *sep;
  g_autoptr (ClutterContent) content = NULL;

  sep = clutter_actor_new ();
  clutter_actor_set_reactive (sep, FALSE);
  clutter_actor_set_size (sep, AQUA_MENU_WIDTH, AQUA_MENU_SEPARATOR_H);
  clutter_actor_set_position (sep, 0.0f, y);
  content = ooze_aqua_menu_separator_content (menu->popup, (int) AQUA_MENU_WIDTH);
  if (content)
    ooze_aqua_actor_set_content (sep,
                               g_steal_pointer (&content),
                               (int) AQUA_MENU_WIDTH,
                               (int) AQUA_MENU_SEPARATOR_H);
  clutter_actor_add_child (menu->rows, sep);
  clutter_actor_show (sep);
}

static void
ooze_aqua_menu_clear_rows (OozeAquaMenu *menu)
{
  if (!menu->rows)
    return;

  clutter_actor_destroy_all_children (menu->rows);
  clutter_actor_hide (menu->highlight);
}

static gfloat
ooze_aqua_menu_rebuild (OozeAquaMenu            *menu,
                      const OozeAquaMenuEntry *entries,
                      gsize                  n_entries)
{
  gfloat y = AQUA_MENU_PAD;
  gsize i;

  ooze_aqua_menu_clear_rows (menu);

  for (i = 0; i < n_entries; i++)
    {
      if (!entries[i].label)
        {
          ooze_aqua_menu_add_separator (menu, y);
          y += AQUA_MENU_SEPARATOR_H;
          continue;
        }

      ooze_aqua_menu_add_row (menu,
                            entries[i].label,
                            entries[i].action_id,
                            entries[i].sensitive,
                            y);
      y += AQUA_MENU_ROW_HEIGHT;
    }

  return y + AQUA_MENU_PAD;
}

static void
ooze_aqua_menu_resize (OozeAquaMenu *menu,
                     gfloat       height)
{
  g_autoptr (ClutterContent) background = NULL;

  clutter_actor_set_size (menu->popup, AQUA_MENU_WIDTH, height);
  clutter_actor_set_size (menu->rows, AQUA_MENU_WIDTH, height);

  g_clear_pointer (&menu->background, clutter_actor_destroy);

  background = ooze_aqua_menu_popup_background (menu->popup,
                                              (int) AQUA_MENU_WIDTH,
                                              (int) height);
  menu->background = clutter_actor_new ();
  if (background)
    ooze_aqua_actor_set_content (menu->background,
                               g_steal_pointer (&background),
                               (int) AQUA_MENU_WIDTH,
                               (int) height);
  clutter_actor_add_child (menu->popup, menu->background);
  /* Target z-order: background (bottom) → highlight (middle) → rows (top).
   * Placing background just below highlight achieves this regardless of
   * the order actors were originally added. */
  clutter_actor_set_child_below_sibling (menu->popup, menu->background, menu->highlight);
  clutter_actor_show (menu->background);
}

static void
ooze_aqua_menu_reposition (OozeAquaMenu *menu)
{
  gfloat anchor_x;
  gfloat anchor_y;
  gfloat anchor_h;
  gfloat stage_h;
  gfloat stage_w;
  gfloat below;
  gfloat above;
  gfloat visible;
  gfloat x;
  gfloat y;
  const gfloat margin = 8.0f;

  if (!menu->anchor || !menu->stage)
    return;

  /*
   * Freshly rebuilt menu-bar labels may not be allocated yet;
   * transformed coords can be NaN and poison popup layout.
   */
  clutter_actor_get_transformed_position (menu->anchor, &anchor_x, &anchor_y);
  if (!isfinite (anchor_x) || !isfinite (anchor_y))
    {
      ClutterActor *parent = clutter_actor_get_parent (menu->anchor);

      clutter_actor_get_position (menu->anchor, &anchor_x, &anchor_y);
      if (parent)
        {
          gfloat px = 0.0f, py = 0.0f;

          clutter_actor_get_transformed_position (parent, &px, &py);
          if (isfinite (px) && isfinite (py))
            {
              anchor_x += px;
              anchor_y += py;
            }
        }
    }

  if (!isfinite (anchor_x) || !isfinite (anchor_y))
    {
      anchor_x = margin;
      anchor_y = 28.0f;
    }

  anchor_h = clutter_actor_get_height (menu->anchor);
  if (!isfinite (anchor_h) || anchor_h < 1.0f)
    anchor_h = 28.0f;

  stage_h = clutter_actor_get_height (menu->stage);
  stage_w = clutter_actor_get_width (menu->stage);
  if (stage_h < 1.0f)
    stage_h = clutter_actor_get_height (clutter_actor_get_stage (menu->stage));
  if (stage_w < 1.0f)
    stage_w = clutter_actor_get_width (clutter_actor_get_stage (menu->stage));
  if (!isfinite (stage_h) || stage_h < 1.0f)
    stage_h = 800.0f;
  if (!isfinite (stage_w) || stage_w < 1.0f)
    stage_w = 1280.0f;

  below = stage_h - (anchor_y + anchor_h + 2.0f) - margin;
  above = anchor_y - margin;
  if (below < 0.0f)
    below = 0.0f;
  if (above < 0.0f)
    above = 0.0f;

  /* Prefer below the anchor; flip above if that fits more of the menu. */
  if (menu->content_height <= below || below >= above)
    {
      visible = MIN (menu->content_height, below);
      if (visible < AQUA_MENU_ROW_HEIGHT + AQUA_MENU_PAD * 2.0f)
        visible = MIN (menu->content_height, MAX (below, above));
      y = anchor_y + anchor_h + 2.0f;
    }
  else
    {
      visible = MIN (menu->content_height, above);
      y = anchor_y - 2.0f - visible;
    }

  if (visible < 1.0f)
    visible = menu->content_height;
  if (!isfinite (visible) || visible < 1.0f)
    visible = AQUA_MENU_ROW_HEIGHT + AQUA_MENU_PAD * 2.0f;

  x = anchor_x;
  if (x + AQUA_MENU_WIDTH > stage_w - margin)
    x = stage_w - AQUA_MENU_WIDTH - margin;
  if (x < margin)
    x = margin;
  if (!isfinite (x))
    x = margin;
  if (!isfinite (y))
    y = anchor_y + anchor_h + 2.0f;

  menu->scroll_y = 0.0f;
  clutter_actor_set_position (menu->rows, 0.0f, 0.0f);
  ooze_aqua_menu_resize (menu, visible);
  clutter_actor_set_clip_to_allocation (menu->popup,
                                        menu->content_height > visible + 0.5f);
  clutter_actor_set_position (menu->popup, x, y);
  menu->open_x = x;
  menu->open_y = y;
}

static gboolean
ooze_aqua_menu_on_scroll (ClutterActor *actor G_GNUC_UNUSED,
                        ClutterEvent *event,
                        OozeAquaMenu   *menu)
{
  gfloat visible;
  gfloat max_scroll;
  gdouble dx = 0.0, dy = 0.0;
  ClutterScrollDirection dir;

  if (!menu->open)
    return CLUTTER_EVENT_PROPAGATE;

  visible = clutter_actor_get_height (menu->popup);
  max_scroll = menu->content_height - visible;
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

  menu->scroll_y += (gfloat) dy * AQUA_MENU_ROW_HEIGHT;
  if (menu->scroll_y < 0.0f)
    menu->scroll_y = 0.0f;
  if (menu->scroll_y > max_scroll)
    menu->scroll_y = max_scroll;

  clutter_actor_set_position (menu->rows, 0.0f, -menu->scroll_y);
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_aqua_menu_finish_close (gpointer user_data)
{
  OozeAquaMenu *menu = user_data;

  menu->close_timeout = 0;
  menu->closing = FALSE;
  clutter_actor_remove_all_transitions (menu->popup);
  clutter_actor_hide (menu->popup);
  clutter_actor_set_opacity (menu->popup, 255);
  clutter_actor_set_pivot_point (menu->popup, 0.0f, 0.0f);
  return G_SOURCE_REMOVE;
}

static void
ooze_aqua_menu_open (OozeAquaMenu *menu)
{
  if (menu->open)
    return;

  if (menu->close_timeout)
    {
      g_source_remove (menu->close_timeout);
      ooze_aqua_menu_finish_close (menu);
    }

  ooze_aqua_menu_reposition (menu);
  clutter_actor_remove_all_transitions (menu->popup);
  clutter_actor_set_opacity (menu->popup, 0);
  clutter_actor_set_position (menu->popup,
                              menu->open_x,
                              menu->open_y - AQUA_MENU_SLIDE_PX);

  clutter_actor_show (menu->backdrop);
  {
    CoglColor scrim;
    /* Dim the desktop under the popup — Unity/macOS-style so menus read clearly. */
    cogl_color_init_from_4f (&scrim, 0.0f, 0.0f, 0.0f, 0.32f);
    clutter_actor_set_background_color (menu->backdrop, &scrim);
  }
  clutter_actor_show (menu->popup);
  ooze_aqua_menu_raise (menu);
  menu->open = TRUE;
  menu->closing = FALSE;

  clutter_actor_save_easing_state (menu->popup);
  clutter_actor_set_easing_duration (menu->popup, AQUA_MENU_OPEN_MS);
  clutter_actor_set_easing_mode (menu->popup, CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_opacity (menu->popup, 255);
  clutter_actor_set_position (menu->popup, menu->open_x, menu->open_y);
  clutter_actor_restore_easing_state (menu->popup);
}

void
ooze_aqua_menu_raise (OozeAquaMenu *menu)
{
  if (!menu || !menu->stage)
    return;

  clutter_actor_set_child_above_sibling (menu->stage, menu->backdrop, NULL);
  clutter_actor_set_child_above_sibling (menu->stage, menu->popup, NULL);
}

static void
ooze_aqua_menu_do_close (OozeAquaMenu *menu)
{
  if (!menu->open && !menu->closing)
    return;

  menu->open = FALSE;
  menu->closing = FALSE;
  ooze_aqua_menu_set_hover (menu, NULL, FALSE);

  if (menu->close_timeout)
    {
      g_source_remove (menu->close_timeout);
      menu->close_timeout = 0;
    }

  clutter_actor_remove_all_transitions (menu->popup);
  clutter_actor_hide (menu->backdrop);
  clutter_actor_hide (menu->popup);
  clutter_actor_set_opacity (menu->popup, 255);
  clutter_actor_set_pivot_point (menu->popup, 0.0f, 0.0f);
}

static gboolean
ooze_aqua_menu_backdrop_pressed (ClutterActor *actor G_GNUC_UNUSED,
                               ClutterEvent *event G_GNUC_UNUSED,
                               OozeAquaMenu   *menu)
{
  ooze_aqua_menu_do_close (menu);
  return CLUTTER_EVENT_STOP;
}

OozeAquaMenu *
ooze_aqua_menu_new (MetaContext          *context,
                  ClutterActor         *stage,
                  OozeAquaMenuActionFunc  callback,
                  gpointer              user_data)
{
  OozeAquaMenu *menu;

  menu = g_new0 (OozeAquaMenu, 1);
  menu->context = context;
  menu->stage = stage;
  menu->callback = callback;
  menu->user_data = user_data;

  menu->backdrop = clutter_actor_new ();
  clutter_actor_set_reactive (menu->backdrop, TRUE);
  clutter_actor_set_size (menu->backdrop, 8192.0f, 8192.0f);
  clutter_actor_set_position (menu->backdrop, -4096.0f, -4096.0f);
  {
    CoglColor scrim;
    cogl_color_init_from_4f (&scrim, 0.0f, 0.0f, 0.0f, 0.32f);
    clutter_actor_set_background_color (menu->backdrop, &scrim);
  }
  clutter_actor_hide (menu->backdrop);
  g_signal_connect (menu->backdrop,
                    "button-press-event",
                    G_CALLBACK (ooze_aqua_menu_backdrop_pressed),
                    menu);
  clutter_actor_add_child (stage, menu->backdrop);

  menu->popup = clutter_actor_new ();
  clutter_actor_set_reactive (menu->popup, TRUE);
  clutter_actor_hide (menu->popup);
  g_signal_connect (menu->popup, "scroll-event",
                    G_CALLBACK (ooze_aqua_menu_on_scroll), menu);

  menu->rows = clutter_actor_new ();
  clutter_actor_set_reactive (menu->rows, FALSE);
  clutter_actor_add_child (menu->popup, menu->rows);
  clutter_actor_show (menu->rows);

  menu->highlight = clutter_actor_new ();
  clutter_actor_set_reactive (menu->highlight, FALSE);
  clutter_actor_set_size (menu->highlight,
                          AQUA_MENU_WIDTH,
                          AQUA_MENU_ROW_HEIGHT);
  clutter_actor_hide (menu->highlight);
  clutter_actor_add_child (menu->popup, menu->highlight);
  /* highlight must be BELOW rows so the (white) label text renders on top
   * of the blue selection rectangle rather than being hidden underneath it. */
  clutter_actor_set_child_below_sibling (menu->popup, menu->highlight, menu->rows);

  clutter_actor_add_child (stage, menu->popup);

  return menu;
}

void
ooze_aqua_menu_attach_anchor (OozeAquaMenu   *menu,
                            ClutterActor *anchor)
{
  if (!menu || !anchor)
    return;

  menu->anchor = anchor;
}

void
ooze_aqua_menu_show_for_anchor (OozeAquaMenu            *menu,
                              ClutterActor          *anchor,
                              const OozeAquaMenuEntry *entries,
                              gsize                  n_entries)
{
  gfloat height;

  if (!menu || !anchor || !entries || n_entries == 0)
    return;

  menu->anchor = anchor;
  height = ooze_aqua_menu_rebuild (menu, entries, n_entries);
  menu->content_height = height;
  menu->scroll_y = 0.0f;
  clutter_actor_set_position (menu->rows, 0.0f, 0.0f);
  /* Temporary size; open/reposition clamps to available screen space. */
  ooze_aqua_menu_resize (menu, height);

  if (menu->open)
    {
      ooze_aqua_menu_reposition (menu);
      ooze_aqua_menu_raise (menu);
      return;
    }

  ooze_aqua_menu_open (menu);
}

void
ooze_aqua_menu_toggle_for_anchor (OozeAquaMenu            *menu,
                                ClutterActor          *anchor,
                                const OozeAquaMenuEntry *entries,
                                gsize                  n_entries)
{
  if (menu->open && menu->anchor == anchor)
    {
      ooze_aqua_menu_do_close (menu);
      return;
    }

  ooze_aqua_menu_show_for_anchor (menu, anchor, entries, n_entries);
}

void
ooze_aqua_menu_close (OozeAquaMenu *menu)
{
  if (!menu)
    return;

  ooze_aqua_menu_do_close (menu);
}

gboolean
ooze_aqua_menu_is_open (OozeAquaMenu *menu)
{
  return menu && menu->open;
}

gboolean
ooze_aqua_menu_handle_key (OozeAquaMenu   *menu,
                         ClutterEvent *event)
{
  if (!menu || !menu->open)
    return FALSE;

  if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_Escape)
    {
      ooze_aqua_menu_do_close (menu);
      return TRUE;
    }

  return FALSE;
}

void
ooze_aqua_menu_destroy (OozeAquaMenu *menu)
{
  if (!menu)
    return;

  if (menu->close_timeout)
    {
      g_source_remove (menu->close_timeout);
      menu->close_timeout = 0;
    }

  g_clear_pointer (&menu->backdrop, clutter_actor_destroy);
  g_clear_pointer (&menu->popup, clutter_actor_destroy);
  g_free (menu);
}
