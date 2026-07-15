#include "ooze-button.h"
#include "ooze-draw.h"
#include "ooze-icons.h"
#include "ooze-palette.h"
#include "ooze-theme.h"

#include <adwaita.h>
#include <graphene.h>
#include <math.h>

/*
 * Layout contract — MAIN BAR tiles (Ooze Gel cloth strip under the title bar)
 * ──────────────────────────────────────────────────────────────────────────
 * CSS padding is 0. Measure and size_allocate own the air around the child.
 *   OOZE_BTN_EDGE       – outset fallback: allocation edge → glass rim
 *                          (used only when there's no tracked icon)
 *   OOZE_BTN_ICON_OUTSET – outset: icon rect → glass rim (icon-only plate)
 *   OOZE_BTN_PAD_*       – inset:  glass rim → icon + caption
 *
 * The glass plate hugs only the icon glyph — never the caption — so labels
 * stay at full contrast in every state (hover / active / pressed).
 */
#define OOZE_BTN_EDGE         5.0
#define OOZE_BTN_ICON_OUTSET  6.0
/* Adwaita image-button ~5px; must be >= ICON_OUTSET so glass does not clamp. */
#define OOZE_BTN_PAD_X        6
#define OOZE_BTN_PAD_Y        6

struct _OozeButton
{
  GtkButton      parent_instance;
  OozeButtonKind kind;
  GtkWidget     *icon;   /* tracked for icon-only glass plate; may be NULL */
};

struct _OozeButtonClass
{
  GtkButtonClass parent_class;
};

G_DEFINE_FINAL_TYPE (OozeButton, ooze_button, GTK_TYPE_BUTTON)

/* ── CSS (loaded once per process) ─────────────────────────────────────── */

static void
ooze_button_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay     *display;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  GtkCssProvider *p = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (p,
    /* padding: 0 — air comes from measure/allocate, not CSS. */
    ".ooze-button {"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "  outline: none;"
    "  padding: 0;"
    "  color: @window_fg_color;"
    "}"
    ".ooze-button:hover,"
    ".ooze-button:active,"
    ".ooze-button:checked {"
    "  background: none;"
    "  box-shadow: none;"
    "}"
    ".ooze-button:focus:not(:focus-visible) {"
    "  outline: none;"
    "}"
    ".ooze-button .ooze-icon {"
    "  min-width: 40px;"
    "  min-height: 40px;"
    "}"
    /* Caption air lives here (box spacing is 0 — avoid double gaps). */
    ".ooze-button .ooze-button-label {"
    "  font-size: 9pt;"
    "  margin-top: 4px;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (p);
  loaded = TRUE;
}

/* ── Measure / allocate ─────────────────────────────────────────────────── */

static void
ooze_button_measure (GtkWidget      *widget,
                     GtkOrientation  orientation,
                     int             for_size,
                     int            *minimum,
                     int            *natural,
                     int            *minimum_baseline,
                     int            *natural_baseline)
{
  GtkWidget *child;
  int        parent_min = 0;
  int        parent_nat = 0;
  int        child_min = 0;
  int        child_nat = 0;
  int        pad;
  int        child_for;

  GTK_WIDGET_CLASS (ooze_button_parent_class)->measure (widget,
                                                       orientation,
                                                       for_size,
                                                       &parent_min,
                                                       &parent_nat,
                                                       minimum_baseline,
                                                       natural_baseline);

  pad = (orientation == GTK_ORIENTATION_HORIZONTAL)
        ? (OOZE_BTN_PAD_X * 2)
        : (OOZE_BTN_PAD_Y * 2);

  child = gtk_button_get_child (GTK_BUTTON (widget));
  if (child)
    {
      child_for = (for_size > pad) ? (for_size - pad) : for_size;
      gtk_widget_measure (child,
                          orientation,
                          child_for,
                          &child_min,
                          &child_nat,
                          NULL,
                          NULL);
    }

  *minimum = MAX (parent_min, child_min + pad);
  *natural = MAX (parent_nat, child_nat + pad);
}

static void
ooze_button_size_allocate (GtkWidget *widget,
                           int        width,
                           int        height,
                           int        baseline G_GNUC_UNUSED)
{
  GtkWidget *child = gtk_button_get_child (GTK_BUTTON (widget));
  int        avail_w;
  int        avail_h;
  int        child_min_w = 0, child_nat_w = 0;
  int        child_min_h = 0, child_nat_h = 0;
  int        child_w, child_h;
  int        x, y;

  avail_w = width - OOZE_BTN_PAD_X * 2;
  avail_h = height - OOZE_BTN_PAD_Y * 2;
  if (avail_w < 0)
    avail_w = 0;
  if (avail_h < 0)
    avail_h = 0;

  if (!child)
    return;

  gtk_widget_measure (child, GTK_ORIENTATION_HORIZONTAL, avail_h,
                      &child_min_w, &child_nat_w, NULL, NULL);
  gtk_widget_measure (child, GTK_ORIENTATION_VERTICAL, avail_w,
                      &child_min_h, &child_nat_h, NULL, NULL);

  child_w = CLAMP (child_nat_w, child_min_w, avail_w > 0 ? avail_w : child_min_w);
  child_h = CLAMP (child_nat_h, child_min_h, avail_h > 0 ? avail_h : child_min_h);

  x = OOZE_BTN_PAD_X + (avail_w - child_w) / 2;
  y = OOZE_BTN_PAD_Y + (avail_h - child_h) / 2;

  gtk_widget_allocate (child, child_w, child_h, -1,
                       gsk_transform_translate (
                         NULL,
                         &GRAPHENE_POINT_INIT ((float) x, (float) y)));
}

/* ── Snapshot ───────────────────────────────────────────────────────────── */

/* Icon rect relative to the button, outset for the glass rim. Falls back to
 * the full allocation (old behavior) only when there's no tracked icon.
 *
 * Do NOT clamp into the tile — the rim is meant to overhang into the
 * toolbar’s Adwaita padding. OozeButton / OozeToolbar use OVERFLOW_VISIBLE
 * so the rounded top is not cropped by the Gel join. */
static void
ooze_button_plate_rect (OozeButton *self,
                        int         ww,
                        int         wh,
                        double     *x,
                        double     *y,
                        double     *w,
                        double     *h)
{
  graphene_rect_t bounds;

  if (self->icon
      && gtk_widget_compute_bounds (self->icon, GTK_WIDGET (self), &bounds))
    {
      *x = bounds.origin.x - OOZE_BTN_ICON_OUTSET;
      *y = bounds.origin.y - OOZE_BTN_ICON_OUTSET;
      *w = bounds.size.width  + OOZE_BTN_ICON_OUTSET * 2.0;
      *h = bounds.size.height + OOZE_BTN_ICON_OUTSET * 2.0;
    }
  else
    {
      *x = OOZE_BTN_EDGE;
      *y = OOZE_BTN_EDGE;
      *w = (double) ww - OOZE_BTN_EDGE * 2.0;
      *h = (double) wh - OOZE_BTN_EDGE * 2.0;
    }

  if (*w < 0.0)
    *w = 0.0;
  if (*h < 0.0)
    *h = 0.0;
}

static void
ooze_button_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  OozeButton        *self  = OOZE_BUTTON (widget);
  const OozePalette *pal   = ooze_palette_current ();
  GtkStateFlags      flags = gtk_widget_get_state_flags (widget);
  int                ww    = gtk_widget_get_width (widget);
  int                wh    = gtk_widget_get_height (widget);
  OozeBtnState       state;
  cairo_t           *cr;
  double             px, py, pw, ph;
  float              x0, y0, x1, y1;

  gboolean pressed = (flags & GTK_STATE_FLAG_ACTIVE)   != 0;
  gboolean toggled = gtk_widget_has_css_class (widget, "active");
  gboolean hovered = (flags & GTK_STATE_FLAG_PRELIGHT) != 0;

  if      (pressed)             state = OOZE_BTN_PRESSED;
  else if (toggled)             state = OOZE_BTN_ACTIVE;
  else if (hovered && !toggled) state = OOZE_BTN_HOVER;
  else                          state = OOZE_BTN_NORMAL;

  ooze_button_plate_rect (self, ww, wh, &px, &py, &pw, &ph);

  if (state != OOZE_BTN_NORMAL && pw >= 4.0 && ph >= 4.0)
    {
      /* Snapshot bounds must cover any overhang past the allocation so the
       * rounded rim can paint into the toolbar’s padding (OVERFLOW_VISIBLE). */
      x0 = (float) floor (MIN (0.0, px) - 1.0);
      y0 = (float) floor (MIN (0.0, py) - 1.0);
      x1 = (float) ceil  (MAX ((double) ww, px + pw) + 1.0);
      y1 = (float) ceil  (MAX ((double) wh, py + ph) + 1.0);

      cr = gtk_snapshot_append_cairo (snapshot,
             &GRAPHENE_RECT_INIT (x0, y0, x1 - x0, y1 - y0));
      /* append_cairo origin is the rect’s top-left — shift draw coords. */
      ooze_draw_button_bg (cr, px - (double) x0, py - (double) y0,
                           pw, ph, state, pal);
      cairo_destroy (cr);
    }

  /* Icon + caption on top of the plate. */
  GTK_WIDGET_CLASS (ooze_button_parent_class)->snapshot (widget, snapshot);
}

/* ── GObject boilerplate ─────────────────────────────────────────────────── */

static void
ooze_button_class_init (OozeButtonClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  /* Own measure/allocate. Clear GtkButton's layout manager in init — do not
   * pass G_TYPE_NONE to set_layout_manager_type (it must be a LayoutManager). */
  widget_class->measure       = ooze_button_measure;
  widget_class->size_allocate = ooze_button_size_allocate;
  widget_class->snapshot      = ooze_button_snapshot;
}

static void
ooze_button_init (OozeButton *self)
{
  ooze_button_ensure_css ();
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-button");
  /* Belt-and-suspenders: parent type may still install a layout manager. */
  gtk_widget_set_layout_manager (GTK_WIDGET (self), NULL);
  /* Glass rim may overhang the tile into toolbar padding. */
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_VISIBLE);

  ooze_theme_connect_dark_notify (G_OBJECT (self),
                                  G_CALLBACK (gtk_widget_queue_draw));
}

/* ── Constructors ────────────────────────────────────────────────────────── */

GtkWidget *
ooze_button_new (OozeButtonKind kind)
{
  OozeButton *b = g_object_new (OOZE_TYPE_BUTTON, NULL);
  b->kind = kind;
  if (kind == OOZE_BUTTON_TOOLBAR)
    {
      gtk_widget_add_css_class (GTK_WIDGET (b), "ooze-toolbar-btn");
      /* Natural tile height — never stretch to fill a tall MAIN BAR. */
      gtk_widget_set_valign (GTK_WIDGET (b), GTK_ALIGN_CENTER);
      gtk_widget_set_vexpand (GTK_WIDGET (b), FALSE);
    }
  return GTK_WIDGET (b);
}

GtkWidget *
ooze_button_new_labeled (OozeButtonKind      kind,
                         const char * const *icon_names,
                         int                 icon_px,
                         const char         *label,
                         const char         *tooltip)
{
  GtkWidget  *btn = ooze_button_new (kind);
  OozeButton *self = OOZE_BUTTON (btn);
  GtkWidget  *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget  *image;
  GtkWidget  *lbl;

  if (icon_px <= 0)
    icon_px = OOZE_ICON_SIZE_TOOLBAR;

  image = ooze_icon_image_new (icon_names, icon_px);
  self->icon = image;

  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), image);

  lbl = gtk_label_new (label ? label : "");
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.5);
  gtk_widget_add_css_class (lbl, "ooze-button-label");
  gtk_box_append (GTK_BOX (box), lbl);

  gtk_button_set_child (GTK_BUTTON (btn), box);
  if (tooltip && tooltip[0])
    gtk_widget_set_tooltip_text (btn, tooltip);
  else if (label && label[0])
    gtk_widget_set_tooltip_text (btn, label);
  return btn;
}

GtkWidget *
ooze_button_new_toolbar (const char * const *icon_names,
                         const char         *label,
                         const char         *tooltip)
{
  return ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR, icon_names,
                                  OOZE_ICON_SIZE_TOOLBAR, label, tooltip);
}

GtkWidget *
ooze_button_new_icon (OozeButtonKind kind, const char * const *icon_names)
{
  const char *label = "Button";
  if (icon_names && icon_names[0])
    {
      if (g_str_has_prefix (icon_names[0], "go-previous"))
        label = "Back";
      else if (g_str_has_prefix (icon_names[0], "go-next"))
        label = "Forward";
      else
        label = icon_names[0];
    }
  return ooze_button_new_labeled (kind, icon_names, OOZE_ICON_SIZE_TOOLBAR,
                                  label, label);
}

void
ooze_button_set_toggled (GtkWidget *button, gboolean toggled)
{
  if (!button)
    return;

  if (toggled)
    gtk_widget_add_css_class (button, "active");
  else
    gtk_widget_remove_css_class (button, "active");

  gtk_widget_queue_draw (button);
}

gboolean
ooze_button_get_toggled (GtkWidget *button)
{
  if (!button)
    return FALSE;
  return gtk_widget_has_css_class (button, "active");
}

void
ooze_button_set_exclusive (GtkWidget **peers,
                           gsize       n_peers,
                           gsize       active)
{
  gsize i;

  if (!peers || n_peers == 0)
    return;

  for (i = 0; i < n_peers; i++)
    ooze_button_set_toggled (peers[i], i == active);
}
