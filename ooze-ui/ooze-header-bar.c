#include "ooze-header-bar.h"

#include "ooze-traffic-lights.h"
#include "ooze-gel.h"

#include "aqua-chrome.h"

/* OozeKit drawing primitives */
#include "ooze-draw.h"
#include "ooze-palette.h"
#include "ooze-surface.h"   /* for the CSS class name constant */
#include "ooze-theme.h"

#include <adwaita.h>

struct _OozeHeaderBar
{
  GtkBox     parent_instance;
  GtkWidget *traffic;
  GtkWidget *title_label;
  GtkWindow *window;
};

G_DEFINE_FINAL_TYPE (OozeHeaderBar, ooze_header_bar, GTK_TYPE_BOX)

/* ── CSS (layout + typography only) ─────────────────────────────────────── */

static void
ooze_header_bar_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay     *display;

  if (loaded)
    return;

  ooze_theme_ensure ();

  display = gdk_display_get_default ();
  if (!display)
    return;

  GtkCssProvider *provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider,
    /* The gradient is drawn in Cairo; GTK must not paint a background here. */
    ".ooze-header-bar {"
    "  min-height: 32px;"
    "  padding: 0;"
    "  border-radius: 0;"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    /* Title colour follows Adwaita so it adapts to light/dark automatically.
     * Weight/size come from ooze_theme_ensure() (Inter 11 regular). */
    ".ooze-header-title {"
    "  color: @window_fg_color;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

/* ── Snapshot ────────────────────────────────────────────────────────────── */

static void
ooze_header_bar_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  const OozePalette *pal = ooze_palette_current ();
  int                w   = gtk_widget_get_width (widget);
  int                h   = gtk_widget_get_height (widget);
  cairo_t           *cr;

  if (w > 0 && h > 0)
    {
      cr = gtk_snapshot_append_cairo (snapshot,
             &GRAPHENE_RECT_INIT (0.f, 0.f, (float) w, (float) h));

      ooze_draw_surface   (cr, w, h,
                           pal->header_r, pal->header_g, pal->header_b,
                           ooze_stripe_origin_y (widget), pal);
      ooze_draw_separator (cr, w, h, OOZE_SIDE_BOTTOM, pal);

      cairo_destroy (cr);
    }

  /* Traffic lights and title label render on top. */
  GTK_WIDGET_CLASS (ooze_header_bar_parent_class)->snapshot (widget, snapshot);
}

/* ── GObject boilerplate ─────────────────────────────────────────────────── */

static void
ooze_header_bar_dispose (GObject *object)
{
  OozeHeaderBar *self = OOZE_HEADER_BAR (object);

  self->window = NULL;
  G_OBJECT_CLASS (ooze_header_bar_parent_class)->dispose (object);
}

static void
ooze_header_bar_class_init (OozeHeaderBarClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose  = ooze_header_bar_dispose;
  widget_class->snapshot = ooze_header_bar_snapshot;
}

static void
ooze_header_bar_init (OozeHeaderBar *self)
{
  GtkWidget *overlay;
  GtkWidget *controls;
  GtkWidget *spacer;

  ooze_header_bar_ensure_css ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (self), 0);
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-header-bar");
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, AQUA_TITLEBAR_HEIGHT);

  /* Redraw when the system theme flips. */
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);

  /* Full-bar overlay so the title centers on the window, not in the
   * leftover space after the traffic lights (classic Aqua). */
  overlay = gtk_overlay_new ();
  gtk_widget_set_hexpand (overlay, TRUE);
  gtk_widget_set_halign (overlay, GTK_ALIGN_FILL);
  gtk_widget_set_valign (overlay, GTK_ALIGN_FILL);
  gtk_box_append (GTK_BOX (self), overlay);

  controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (controls, TRUE);
  gtk_widget_set_halign (controls, GTK_ALIGN_FILL);
  gtk_widget_set_valign (controls, GTK_ALIGN_FILL);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), controls);

  self->traffic = GTK_WIDGET (ooze_traffic_lights_new ());
  gtk_widget_set_valign (self->traffic, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (controls), self->traffic);

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (controls), spacer);

  self->title_label = gtk_label_new ("");
  gtk_widget_add_css_class (self->title_label, "ooze-header-title");
  gtk_label_set_ellipsize (GTK_LABEL (self->title_label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign (self->title_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->title_label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (self->title_label, 8);
  gtk_widget_set_margin_end (self->title_label, 8);
  /* Pass clicks through so traffic lights stay hittable under long titles. */
  gtk_widget_set_can_target (self->title_label, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), self->title_label);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

OozeHeaderBar *
ooze_header_bar_new (void)
{
  return g_object_new (OOZE_TYPE_HEADER_BAR, NULL);
}

void
ooze_header_bar_attach_window (OozeHeaderBar *self,
                               GtkWindow     *window)
{
  g_return_if_fail (OOZE_IS_HEADER_BAR (self));
  g_return_if_fail (GTK_IS_WINDOW (window));

  self->window = window;
  ooze_traffic_lights_attach_window (OOZE_TRAFFIC_LIGHTS (self->traffic), window);
  ooze_gel_install_drag (GTK_WIDGET (self), window);

  /* Install mid-edge resize grips once the window content exists. */
  if (gtk_window_get_child (window))
    ooze_gel_install_edge_resize (window);
  else if (!g_object_get_data (G_OBJECT (window), "ooze-edge-resize-watch"))
    {
      g_object_set_data (G_OBJECT (window), "ooze-edge-resize-watch", GINT_TO_POINTER (1));
      g_signal_connect (window, "notify::child",
                        G_CALLBACK (ooze_gel_install_edge_resize), NULL);
    }
}

void
ooze_header_bar_set_title (OozeHeaderBar *self,
                           const char    *title)
{
  g_return_if_fail (OOZE_IS_HEADER_BAR (self));

  gtk_label_set_text (GTK_LABEL (self->title_label), title ? title : "");
}

void
ooze_header_bar_set_title_widget (OozeHeaderBar *self,
                                  GtkWidget     *widget)
{
  g_return_if_fail (OOZE_IS_HEADER_BAR (self));

  if (widget)
    {
      const char *text = gtk_widget_get_name (widget);
      if (GTK_IS_LABEL (widget))
        text = gtk_label_get_text (GTK_LABEL (widget));
      ooze_header_bar_set_title (self, text);
    }
  else
    {
      ooze_header_bar_set_title (self, "");
    }
}
