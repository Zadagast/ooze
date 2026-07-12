#include "ooze-surface.h"
#include "ooze-draw.h"
#include "ooze-palette.h"

#include <adwaita.h>

struct _OozeSurface
{
  GtkBox             parent_instance;
  OozeSurfaceVariant variant;
};

struct _OozeSurfaceClass
{
  GtkBoxClass parent_class;
};

G_DEFINE_FINAL_TYPE (OozeSurface, ooze_surface, GTK_TYPE_BOX)

/* ── CSS (loaded once per process) ─────────────────────────────────────── */

static void
ooze_surface_ensure_css (void)
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
    /* OozeSurface draws all chrome in Cairo; let GTK render nothing. */
    ".ooze-surface {"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "  padding: 0;"
    "  border-radius: 0;"
    "}"
    /*
     * Status bar chrome stays edge-to-edge (connected to the window frame).
     * Only children are inset so glyphs clear the CSD corner radius.
     */
    ".ooze-surface-statusbar {"
    "  min-height: 24px;"
    "  padding: 0;"
    "}"
    ".ooze-surface-statusbar > * {"
    "  margin-top: 3px;"
    "  margin-bottom: 8px;"
    "  margin-left: 10px;"
    "  margin-right: 10px;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (p);
  loaded = TRUE;
}

/* ── Snapshot ───────────────────────────────────────────────────────────── */

static void
ooze_surface_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  OozeSurface       *self = OOZE_SURFACE (widget);
  const OozePalette *pal  = ooze_palette_current ();
  int                w    = gtk_widget_get_width (widget);
  int                h    = gtk_widget_get_height (widget);
  double             r, g, b;
  OozeSide           sep_side;
  cairo_t           *cr;

  if (w <= 0 || h <= 0)
    goto children;

  switch (self->variant)
    {
    case OOZE_SURFACE_HEADER:
      r = pal->header_r;    g = pal->header_g;    b = pal->header_b;
      sep_side = OOZE_SIDE_BOTTOM;
      break;
    case OOZE_SURFACE_TOOLBAR:
      r = pal->toolbar_r;   g = pal->toolbar_g;   b = pal->toolbar_b;
      sep_side = OOZE_SIDE_BOTTOM;
      break;
    case OOZE_SURFACE_SIDEBAR:
      r = pal->sidebar_r;   g = pal->sidebar_g;   b = pal->sidebar_b;
      sep_side = OOZE_SIDE_RIGHT;
      break;
    case OOZE_SURFACE_STATUSBAR:
      r = pal->statusbar_r; g = pal->statusbar_g; b = pal->statusbar_b;
      sep_side = OOZE_SIDE_TOP;
      break;
    default:
      goto children;
    }

  cr = gtk_snapshot_append_cairo (snapshot,
         &GRAPHENE_RECT_INIT (0.f, 0.f, (float) w, (float) h));

  ooze_draw_surface   (cr, w, h, r, g, b, pal);
  ooze_draw_separator (cr, w, h, sep_side, pal);

  cairo_destroy (cr);

children:
  GTK_WIDGET_CLASS (ooze_surface_parent_class)->snapshot (widget, snapshot);
}

/* ── GObject boilerplate ─────────────────────────────────────────────────── */

static void
ooze_surface_class_init (OozeSurfaceClass *klass)
{
  GTK_WIDGET_CLASS (klass)->snapshot = ooze_surface_snapshot;
}

static void
ooze_surface_init (OozeSurface *self)
{
  ooze_surface_ensure_css ();
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-surface");

  /* Redraw whenever the colour-scheme flips; auto-disconnects on destroy. */
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */

GtkWidget *
ooze_surface_new (OozeSurfaceVariant variant, GtkOrientation orientation)
{
  OozeSurface *s = g_object_new (OOZE_TYPE_SURFACE, NULL);
  s->variant = variant;
  gtk_orientable_set_orientation (GTK_ORIENTABLE (s), orientation);

  if (variant == OOZE_SURFACE_STATUSBAR)
    gtk_widget_add_css_class (GTK_WIDGET (s), "ooze-surface-statusbar");

  return GTK_WIDGET (s);
}
