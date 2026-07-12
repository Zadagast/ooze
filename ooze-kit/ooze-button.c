#include "ooze-button.h"
#include "ooze-draw.h"
#include "ooze-icons.h"
#include "ooze-palette.h"

#include <adwaita.h>

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
    /* Strip GTK's own button chrome so Cairo is the only painter. */
    ".ooze-button {"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "  outline: none;"
    "  padding: 2px 6px;"
    "  min-height: 0;"
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
    /* Keep toolbar icon glyphs in a uniform square. */
    ".ooze-button .ooze-icon {"
    "  min-width: 32px;"
    "  min-height: 32px;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (p);
  loaded = TRUE;
}

/* ── Snapshot ───────────────────────────────────────────────────────────── */

static void
ooze_button_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  const OozePalette *pal   = ooze_palette_current ();
  GtkStateFlags      flags = gtk_widget_get_state_flags (widget);
  int                w     = gtk_widget_get_width (widget);
  int                h     = gtk_widget_get_height (widget);
  OozeBtnState       state;
  cairo_t           *cr;

  gboolean pressed = (flags & GTK_STATE_FLAG_ACTIVE)   != 0;
  gboolean toggled = gtk_widget_has_css_class (widget, "active");
  gboolean hovered = (flags & GTK_STATE_FLAG_PRELIGHT) != 0;

  if      (pressed)            state = OOZE_BTN_PRESSED;
  else if (toggled)            state = OOZE_BTN_ACTIVE;
  else if (hovered && !toggled) state = OOZE_BTN_HOVER;
  else                         state = OOZE_BTN_NORMAL;

  if (state != OOZE_BTN_NORMAL && w > 0 && h > 0)
    {
      cr = gtk_snapshot_append_cairo (snapshot,
             &GRAPHENE_RECT_INIT (0.f, 0.f, (float) w, (float) h));
      ooze_draw_button_bg (cr, w, h, state, pal);
      cairo_destroy (cr);
    }

  GTK_WIDGET_CLASS (ooze_button_parent_class)->snapshot (widget, snapshot);
}

/* ── GObject boilerplate ─────────────────────────────────────────────────── */

static void
ooze_button_class_init (OozeButtonClass *klass)
{
  GTK_WIDGET_CLASS (klass)->snapshot = ooze_button_snapshot;
}

static void
ooze_button_init (OozeButton *self G_GNUC_UNUSED)
{
  ooze_button_ensure_css ();
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-button");

  /* Redraw on theme flip so hover/press tints use the right palette. */
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
  /* Legacy API: still emits a labeled color button (never icon-only). */
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
