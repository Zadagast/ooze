#include "ooze-button.h"
#include "ooze-draw.h"
#include "ooze-icons.h"
#include "ooze-palette.h"

#include <adwaita.h>
#include <graphene.h>

/*
 * Layout contract
 * ───────────────
 * CSS padding is 0. Measure and size_allocate own the air around the child
 * (OOZE_BTN_PAD_*). Glass plate = allocation inset by OOZE_BTN_EDGE only.
 */
#define OOZE_BTN_EDGE  2.0
#define OOZE_BTN_PAD_X 10
#define OOZE_BTN_PAD_Y 8

struct _OozeButton
{
  GtkButton      parent_instance;
  OozeButtonKind kind;
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
    "  min-width: 24px;"
    "  min-height: 24px;"
    "}"
    ".ooze-button .ooze-button-label {"
    "  margin-top: 2px;"
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

static void
ooze_button_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  const OozePalette *pal   = ooze_palette_current ();
  GtkStateFlags      flags = gtk_widget_get_state_flags (widget);
  int                ww    = gtk_widget_get_width (widget);
  int                wh    = gtk_widget_get_height (widget);
  OozeBtnState       state;
  cairo_t           *cr;
  double             pw, ph;

  gboolean pressed = (flags & GTK_STATE_FLAG_ACTIVE)   != 0;
  gboolean toggled = gtk_widget_has_css_class (widget, "active");
  gboolean hovered = (flags & GTK_STATE_FLAG_PRELIGHT) != 0;

  if      (pressed)             state = OOZE_BTN_PRESSED;
  else if (toggled)             state = OOZE_BTN_ACTIVE;
  else if (hovered && !toggled) state = OOZE_BTN_HOVER;
  else                          state = OOZE_BTN_NORMAL;

  pw = (double) ww - OOZE_BTN_EDGE * 2.0;
  ph = (double) wh - OOZE_BTN_EDGE * 2.0;

  if (state != OOZE_BTN_NORMAL && pw >= 4.0 && ph >= 4.0)
    {
      cr = gtk_snapshot_append_cairo (snapshot,
             &GRAPHENE_RECT_INIT (0.f, 0.f, (float) ww, (float) wh));
      ooze_draw_button_bg (cr, OOZE_BTN_EDGE, OOZE_BTN_EDGE, pw, ph, state, pal);
      cairo_destroy (cr);
    }

  GTK_WIDGET_CLASS (ooze_button_parent_class)->snapshot (widget, snapshot);
}

/* ── GObject boilerplate ─────────────────────────────────────────────────── */

static void
ooze_button_class_init (OozeButtonClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->measure       = ooze_button_measure;
  widget_class->size_allocate = ooze_button_size_allocate;
  widget_class->snapshot      = ooze_button_snapshot;
}

static void
ooze_button_init (OozeButton *self G_GNUC_UNUSED)
{
  ooze_button_ensure_css ();
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-button");

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

/* ── Constructors ────────────────────────────────────────────────────────── */

GtkWidget *
ooze_button_new (OozeButtonKind kind)
{
  OozeButton *b = g_object_new (OOZE_TYPE_BUTTON, NULL);
  b->kind = kind;
  if (kind == OOZE_BUTTON_TOOLBAR)
    gtk_widget_add_css_class (GTK_WIDGET (b), "ooze-toolbar-btn");
  return GTK_WIDGET (b);
}

GtkWidget *
ooze_button_new_labeled (OozeButtonKind      kind,
                         const char * const *icon_names,
                         int                 icon_px,
                         const char         *label,
                         const char         *tooltip)
{
  GtkWidget *btn = ooze_button_new (kind);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  GtkWidget *image;
  GtkWidget *lbl;

  if (icon_px <= 0)
    icon_px = OOZE_ICON_SIZE_TOOLBAR;

  image = ooze_icon_image_new (icon_names, icon_px);

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
