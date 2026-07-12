#include "my-aqua-menu.h"

#include "my-aqua-draw.h"
#include "my-theme.h"

#include <cairo/cairo.h>

#define AQUA_MENU_WIDTH       240.0f
#define AQUA_MENU_ROW_HEIGHT  22.0f
#define AQUA_MENU_PAD         4.0f
#define AQUA_MENU_SEPARATOR_H 9.0f
#define AQUA_MENU_CORNER      6.0f

typedef struct _MyAquaMenu
{
  MetaContext *context;
  ClutterActor *stage;
  ClutterActor *anchor;
  ClutterActor *backdrop;
  ClutterActor *popup;
  ClutterActor *background;
  ClutterActor *rows;
  ClutterActor *highlight;
  MyAquaMenuActionFunc callback;
  gpointer user_data;
  gboolean open;
  gfloat content_height; /* full unclipped menu height */
  gfloat scroll_y;       /* 0 … max(0, content - visible) */
} MyAquaMenu;

static void my_aqua_menu_do_close (MyAquaMenu *menu);
static void my_aqua_menu_open (MyAquaMenu *menu);

static ClutterContent *
my_aqua_menu_popup_background (ClutterActor *ref_actor,
                               int           width,
                               int           height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.18);
  cairo_rectangle (cr, 2.0, 3.0, width - 2.0, height - 1.0);
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
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.35);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  cairo_destroy (cr);
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterContent *
my_aqua_menu_row_highlight (ClutterActor *ref_actor,
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
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterContent *
my_aqua_menu_separator_content (ClutterActor *ref_actor,
                                int           width)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  ClutterContent *content;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);
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
  content = my_aqua_content_from_surface (ref_actor, surface);
  cairo_surface_destroy (surface);

  return content;
}

static ClutterActor *
my_aqua_menu_create_label (ClutterActor *ref_actor,
                           const char   *text,
                           gboolean      inverted,
                           gboolean      dimmed)
{
  ClutterActor *label;
  g_autoptr (ClutterContent) content = NULL;
  const MyAquaPalette *palette = my_theme_get_palette (NULL);
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

  content = my_aqua_text_content (ref_actor,
                                  "Sans 11",
                                  text,
                                  r, g, b,
                                  &width,
                                  &height);
  label = clutter_actor_new ();
  if (content)
    my_aqua_actor_set_content (label,
                               g_steal_pointer (&content),
                               width,
                               height);
  clutter_actor_set_position (label, 18.0f, (AQUA_MENU_ROW_HEIGHT - height) / 2.0f);
  return label;
}

static void
my_aqua_menu_set_hover (MyAquaMenu   *menu,
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
              label = my_aqua_menu_create_label (menu->popup,
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

  highlight = my_aqua_menu_row_highlight (menu->popup,
                                          (int) AQUA_MENU_WIDTH,
                                          (int) AQUA_MENU_ROW_HEIGHT);
  if (highlight)
    my_aqua_actor_set_content (menu->highlight,
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
      label = my_aqua_menu_create_label (menu->popup, text, TRUE, FALSE);
      g_object_set_data (G_OBJECT (row), "menu-label", label);
      clutter_actor_add_child (row, label);
      clutter_actor_show (label);
    }
}

static gboolean
my_aqua_menu_row_enter (ClutterActor *actor,
                        ClutterEvent *event G_GNUC_UNUSED,
                        MyAquaMenu   *menu)
{
  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-sensitive")))
    return CLUTTER_EVENT_PROPAGATE;

  my_aqua_menu_set_hover (menu, actor, TRUE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
my_aqua_menu_row_leave (ClutterActor *actor,
                        ClutterEvent *event G_GNUC_UNUSED,
                        MyAquaMenu   *menu)
{
  my_aqua_menu_set_hover (menu, actor, FALSE);
  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
my_aqua_menu_row_pressed (ClutterActor *actor,
                          ClutterEvent *event,
                          MyAquaMenu   *menu)
{
  int action_id;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-sensitive")))
    return CLUTTER_EVENT_STOP;

  action_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "menu-action"));
  my_aqua_menu_do_close (menu);

  if (menu->callback && action_id != 0)
    menu->callback (menu->user_data, action_id);

  return CLUTTER_EVENT_STOP;
}

static void
my_aqua_menu_add_row (MyAquaMenu   *menu,
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

  label_actor = my_aqua_menu_create_label (menu->popup, label, FALSE, !sensitive);
  g_object_set_data (G_OBJECT (row), "menu-label", label_actor);
  clutter_actor_add_child (row, label_actor);
  clutter_actor_show (label_actor);

  g_signal_connect (row, "enter-event", G_CALLBACK (my_aqua_menu_row_enter), menu);
  g_signal_connect (row, "leave-event", G_CALLBACK (my_aqua_menu_row_leave), menu);
  g_signal_connect (row, "button-press-event", G_CALLBACK (my_aqua_menu_row_pressed), menu);

  clutter_actor_add_child (menu->rows, row);
  clutter_actor_show (row);
}

static void
my_aqua_menu_add_separator (MyAquaMenu *menu,
                            gfloat       y)
{
  ClutterActor *sep;
  g_autoptr (ClutterContent) content = NULL;

  sep = clutter_actor_new ();
  clutter_actor_set_reactive (sep, FALSE);
  clutter_actor_set_size (sep, AQUA_MENU_WIDTH, AQUA_MENU_SEPARATOR_H);
  clutter_actor_set_position (sep, 0.0f, y);
  content = my_aqua_menu_separator_content (menu->popup, (int) AQUA_MENU_WIDTH);
  if (content)
    my_aqua_actor_set_content (sep,
                               g_steal_pointer (&content),
                               (int) AQUA_MENU_WIDTH,
                               (int) AQUA_MENU_SEPARATOR_H);
  clutter_actor_add_child (menu->rows, sep);
  clutter_actor_show (sep);
}

static void
my_aqua_menu_clear_rows (MyAquaMenu *menu)
{
  if (!menu->rows)
    return;

  clutter_actor_destroy_all_children (menu->rows);
  clutter_actor_hide (menu->highlight);
}

static gfloat
my_aqua_menu_rebuild (MyAquaMenu            *menu,
                      const MyAquaMenuEntry *entries,
                      gsize                  n_entries)
{
  gfloat y = AQUA_MENU_PAD;
  gsize i;

  my_aqua_menu_clear_rows (menu);

  for (i = 0; i < n_entries; i++)
    {
      if (!entries[i].label)
        {
          my_aqua_menu_add_separator (menu, y);
          y += AQUA_MENU_SEPARATOR_H;
          continue;
        }

      my_aqua_menu_add_row (menu,
                            entries[i].label,
                            entries[i].action_id,
                            entries[i].sensitive,
                            y);
      y += AQUA_MENU_ROW_HEIGHT;
    }

  return y + AQUA_MENU_PAD;
}

static void
my_aqua_menu_resize (MyAquaMenu *menu,
                     gfloat       height)
{
  g_autoptr (ClutterContent) background = NULL;

  clutter_actor_set_size (menu->popup, AQUA_MENU_WIDTH, height);
  clutter_actor_set_size (menu->rows, AQUA_MENU_WIDTH, height);

  g_clear_pointer (&menu->background, clutter_actor_destroy);

  background = my_aqua_menu_popup_background (menu->popup,
                                              (int) AQUA_MENU_WIDTH,
                                              (int) height);
  menu->background = clutter_actor_new ();
  if (background)
    my_aqua_actor_set_content (menu->background,
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
my_aqua_menu_reposition (MyAquaMenu *menu)
{
  gfloat anchor_x;
  gfloat anchor_y;
  gfloat anchor_h;
  gfloat stage_h;
  gfloat below;
  gfloat above;
  gfloat visible;
  gfloat x;
  gfloat y;
  const gfloat margin = 8.0f;

  if (!menu->anchor || !menu->stage)
    return;

  clutter_actor_get_transformed_position (menu->anchor, &anchor_x, &anchor_y);
  anchor_h = clutter_actor_get_height (menu->anchor);
  stage_h = clutter_actor_get_height (menu->stage);
  if (stage_h < 1.0f)
    stage_h = clutter_actor_get_height (clutter_actor_get_stage (menu->stage));

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

  x = anchor_x;
  if (x + AQUA_MENU_WIDTH > clutter_actor_get_width (menu->stage) - margin)
    x = clutter_actor_get_width (menu->stage) - AQUA_MENU_WIDTH - margin;
  if (x < margin)
    x = margin;

  menu->scroll_y = 0.0f;
  clutter_actor_set_position (menu->rows, 0.0f, 0.0f);
  my_aqua_menu_resize (menu, visible);
  clutter_actor_set_clip_to_allocation (menu->popup,
                                        menu->content_height > visible + 0.5f);
  clutter_actor_set_position (menu->popup, x, y);
}

static gboolean
my_aqua_menu_on_scroll (ClutterActor *actor G_GNUC_UNUSED,
                        ClutterEvent *event,
                        MyAquaMenu   *menu)
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

static void
my_aqua_menu_open (MyAquaMenu *menu)
{
  if (menu->open)
    return;

  my_aqua_menu_reposition (menu);
  clutter_actor_show (menu->backdrop);
  clutter_actor_show (menu->popup);
  my_aqua_menu_raise (menu);
  menu->open = TRUE;
}

void
my_aqua_menu_raise (MyAquaMenu *menu)
{
  if (!menu || !menu->stage)
    return;

  clutter_actor_set_child_above_sibling (menu->stage, menu->backdrop, NULL);
  clutter_actor_set_child_above_sibling (menu->stage, menu->popup, NULL);
}

static void
my_aqua_menu_do_close (MyAquaMenu *menu)
{
  if (!menu->open)
    return;

  my_aqua_menu_set_hover (menu, NULL, FALSE);
  clutter_actor_hide (menu->backdrop);
  clutter_actor_hide (menu->popup);
  menu->open = FALSE;
}

static gboolean
my_aqua_menu_backdrop_pressed (ClutterActor *actor G_GNUC_UNUSED,
                               ClutterEvent *event G_GNUC_UNUSED,
                               MyAquaMenu   *menu)
{
  my_aqua_menu_do_close (menu);
  return CLUTTER_EVENT_STOP;
}

MyAquaMenu *
my_aqua_menu_new (MetaContext          *context,
                  ClutterActor         *stage,
                  MyAquaMenuActionFunc  callback,
                  gpointer              user_data)
{
  MyAquaMenu *menu;

  menu = g_new0 (MyAquaMenu, 1);
  menu->context = context;
  menu->stage = stage;
  menu->callback = callback;
  menu->user_data = user_data;

  menu->backdrop = clutter_actor_new ();
  clutter_actor_set_reactive (menu->backdrop, TRUE);
  clutter_actor_set_size (menu->backdrop, 8192.0f, 8192.0f);
  clutter_actor_set_position (menu->backdrop, -4096.0f, -4096.0f);
  clutter_actor_hide (menu->backdrop);
  g_signal_connect (menu->backdrop,
                    "button-press-event",
                    G_CALLBACK (my_aqua_menu_backdrop_pressed),
                    menu);
  clutter_actor_add_child (stage, menu->backdrop);

  menu->popup = clutter_actor_new ();
  clutter_actor_set_reactive (menu->popup, TRUE);
  clutter_actor_hide (menu->popup);
  g_signal_connect (menu->popup, "scroll-event",
                    G_CALLBACK (my_aqua_menu_on_scroll), menu);

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
my_aqua_menu_attach_anchor (MyAquaMenu   *menu,
                            ClutterActor *anchor)
{
  if (!menu || !anchor)
    return;

  menu->anchor = anchor;
}

void
my_aqua_menu_show_for_anchor (MyAquaMenu            *menu,
                              ClutterActor          *anchor,
                              const MyAquaMenuEntry *entries,
                              gsize                  n_entries)
{
  gfloat height;

  if (!menu || !anchor || !entries || n_entries == 0)
    return;

  menu->anchor = anchor;
  height = my_aqua_menu_rebuild (menu, entries, n_entries);
  menu->content_height = height;
  menu->scroll_y = 0.0f;
  clutter_actor_set_position (menu->rows, 0.0f, 0.0f);
  /* Temporary size; open/reposition clamps to available screen space. */
  my_aqua_menu_resize (menu, height);
  my_aqua_menu_open (menu);
}

void
my_aqua_menu_toggle_for_anchor (MyAquaMenu            *menu,
                                ClutterActor          *anchor,
                                const MyAquaMenuEntry *entries,
                                gsize                  n_entries)
{
  if (menu->open && menu->anchor == anchor)
    {
      my_aqua_menu_do_close (menu);
      return;
    }

  my_aqua_menu_show_for_anchor (menu, anchor, entries, n_entries);
}

void
my_aqua_menu_close (MyAquaMenu *menu)
{
  if (!menu)
    return;

  my_aqua_menu_do_close (menu);
}

gboolean
my_aqua_menu_is_open (MyAquaMenu *menu)
{
  return menu && menu->open;
}

gboolean
my_aqua_menu_handle_key (MyAquaMenu   *menu,
                         ClutterEvent *event)
{
  if (!menu || !menu->open)
    return FALSE;

  if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_Escape)
    {
      my_aqua_menu_do_close (menu);
      return TRUE;
    }

  return FALSE;
}

void
my_aqua_menu_destroy (MyAquaMenu *menu)
{
  if (!menu)
    return;

  g_clear_pointer (&menu->backdrop, clutter_actor_destroy);
  g_clear_pointer (&menu->popup, clutter_actor_destroy);
  g_free (menu);
}
