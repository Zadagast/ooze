#include "ooze-draw.h"
#include "aqua-chrome.h"

#include <gtk/gtk.h>
#include <math.h>
#include <glib.h>   /* G_PI */

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/* Rounded-rectangle path with uniform corner radius r, inset by (x,y). */
static void
rounded_rect (cairo_t *cr,
              double x, double y,
              double w, double h,
              double r)
{
  cairo_new_sub_path (cr);
  cairo_arc (cr, x + r,     y + r,     r, G_PI,         3.0 * G_PI / 2.0);
  cairo_arc (cr, x + w - r, y + r,     r, 3.0 * G_PI / 2.0, 0.0);
  cairo_arc (cr, x + w - r, y + h - r, r, 0.0,           G_PI / 2.0);
  cairo_arc (cr, x + r,     y + h - r, r, G_PI / 2.0,   G_PI);
  cairo_close_path (cr);
}

static gboolean
ooze_widget_in_gel_titlebar (GtkWidget *widget, GtkWindow *window)
{
  GtkWidget *titlebar = gtk_window_get_titlebar (window);
  GtkWidget *w;

  for (w = widget; w != NULL; w = gtk_widget_get_parent (w))
    {
      if (w == titlebar)
        return TRUE;
      if (gtk_widget_has_css_class (w, "titlebar")
          || gtk_widget_has_css_class (w, "ooze-header-bar"))
        return TRUE;
    }

  return FALSE;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int
ooze_stripe_origin_y (GtkWidget *widget)
{
  GtkRoot *root;
  GtkWindow *window;
  GtkWidget *content;
  graphene_point_t out;

  if (!widget)
    return 0;

  root = gtk_widget_get_root (widget);
  if (!GTK_IS_WINDOW (root))
    return 0;

  window = GTK_WINDOW (root);

  /* Ooze Gel titlebar is the cloth origin (CSD subtree). */
  if (ooze_widget_in_gel_titlebar (widget, window))
    return 0;

  content = gtk_window_get_child (window);
  if (!content)
    return AQUA_TITLEBAR_HEIGHT;

  if (!gtk_widget_compute_point (widget, content,
                                 &GRAPHENE_POINT_INIT (0.f, 0.f), &out))
    return AQUA_TITLEBAR_HEIGHT;

  return AQUA_TITLEBAR_HEIGHT + (int) floor (out.y);
}

void
ooze_draw_surface (cairo_t           *cr,
                   int                w,
                   int                h,
                   double             r,
                   double             g,
                   double             b,
                   int                stripe_origin_y,
                   const OozePalette *p)
{
  /* Base fill */
  cairo_set_source_rgb (cr, r, g, b);
  cairo_rectangle (cr, 0, 0, w, h);
  cairo_fill (cr);

  /* Pinstripe: one 1 px tinted line every stride rows, phase-locked to the
   * Ooze Gel pinline grid so flush strips read as one cloth. */
  if (p->pinstripe_alpha > 0.001 && p->pinstripe_stride > 1)
    {
      double pr = p->dark ? 1.0 : 0.0;
      double pg = p->dark ? 1.0 : 0.0;
      double pb = p->dark ? 1.0 : 0.0;
      int    stride = p->pinstripe_stride;
      int    phase = stripe_origin_y % stride;
      int    py;

      if (phase < 0)
        phase += stride;

      cairo_set_source_rgba (cr, pr, pg, pb, p->pinstripe_alpha);
      for (py = -phase; py < h; py += stride)
        {
          if (py < 0)
            continue;
          cairo_rectangle (cr, 0, py, w, 1);
          cairo_fill (cr);
        }
    }
}

void
ooze_draw_separator (cairo_t          *cr,
                     int               w,
                     int               h,
                     OozeSide          side,
                     const OozePalette *p)
{
  double lo_a = p->sep_alpha;

  switch (side)
    {
    case OOZE_SIDE_BOTTOM:
      /* Single groove on the pin grid — part of the Ooze Gel cloth. */
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, h - 1, w, 1);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_TOP:
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, 0, w, 1);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_RIGHT:
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, w - 1, 0, 1, h);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_LEFT:
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, 0, 1, h);
      cairo_fill (cr);
      break;

    default:
      break;
    }
}

void
ooze_draw_button_bg (cairo_t          *cr,
                     double            x,
                     double            y,
                     double            w,
                     double            h,
                     OozeBtnState      state,
                     const OozePalette *p)
{
  /* Radius scales with taller MAIN BAR plates (40px icons + inset pad). */
  const double R = CLAMP (h * 0.22, 6.0, 11.0);

  if (state == OOZE_BTN_NORMAL)
    return;

  if (w < 4.0 || h < 4.0)
    return;

  switch (state)
    {
    case OOZE_BTN_PRESSED:
    case OOZE_BTN_ACTIVE:
      {
        /* Dock-like frosted plate. Light mode uses a soft smoke fill so the
         * plate reads on aluminum toolbars (pure white glass disappears). */
        const gboolean pressed = (state == OOZE_BTN_PRESSED);
        const double glass_r = p->dark ? 0.16 : 0.92;
        const double glass_g = p->dark ? 0.16 : 0.93;
        const double glass_b = p->dark ? 0.18 : 0.96;
        const double smoke_a_top = pressed
          ? (p->dark ? 0.58 : 0.26)
          : (p->dark ? 0.42 : 0.20);
        const double smoke_a_mid = pressed
          ? (p->dark ? 0.44 : 0.18)
          : (p->dark ? 0.30 : 0.14);
        const double smoke_a_bot = pressed
          ? (p->dark ? 0.34 : 0.14)
          : (p->dark ? 0.22 : 0.10);
        const double border_hi = p->dark ? 0.38 : 0.82;
        const double border_lo = p->dark ? 0.32 : 0.36;
        cairo_pattern_t *grad;

        rounded_rect (cr, x, y, w, h, R);

        if (p->dark)
          {
            grad = cairo_pattern_create_linear (0, y, 0, y + h);
            cairo_pattern_add_color_stop_rgba (grad, 0.0,
                                               glass_r, glass_g, glass_b, smoke_a_top);
            cairo_pattern_add_color_stop_rgba (grad, 0.45,
                                               glass_r, glass_g, glass_b, smoke_a_mid);
            cairo_pattern_add_color_stop_rgba (grad, 1.0,
                                               glass_r * 0.92, glass_g * 0.94, glass_b * 0.98,
                                               smoke_a_bot);
            cairo_set_source (cr, grad);
            cairo_fill_preserve (cr);
            cairo_pattern_destroy (grad);
          }
        else
          {
            /* Soft translucent white plate + faint smoke so it lifts off #f0f0f0. */
            grad = cairo_pattern_create_linear (0, y, 0, y + h);
            cairo_pattern_add_color_stop_rgba (grad, 0.0,
                                               1.0, 1.0, 1.0, smoke_a_top + 0.30);
            cairo_pattern_add_color_stop_rgba (grad, 0.5,
                                               glass_r, glass_g, glass_b, smoke_a_mid + 0.20);
            cairo_pattern_add_color_stop_rgba (grad, 1.0,
                                               0.78, 0.80, 0.84, smoke_a_bot + 0.16);
            cairo_set_source (cr, grad);
            cairo_fill_preserve (cr);
            cairo_pattern_destroy (grad);

            cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, pressed ? 0.11 : 0.08);
            cairo_fill_preserve (cr);
          }

        /* Light rim */
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, border_hi);
        cairo_set_line_width (cr, 1.0);
        cairo_stroke (cr);

        /* Dark hairline — stronger in light mode for readability */
        rounded_rect (cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, R);
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, border_lo);
        cairo_set_line_width (cr, 1.0);
        cairo_stroke (cr);

        /* Top gloss */
        rounded_rect (cr, x + 2.5, y + 2.5,
                      w - 5.0, h * 0.38,
                      MAX (R - 2.5, 2.0));
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, p->dark ? 0.18 : 0.50);
        cairo_fill (cr);
      }
      break;

    case OOZE_BTN_HOVER:
      rounded_rect (cr, x, y, w, h, R);
      cairo_set_source_rgba (cr,
                             p->dark ? 1.0 : 0.0,
                             p->dark ? 1.0 : 0.0,
                             p->dark ? 1.0 : 0.0,
                             p->btn_hover_a);
      cairo_fill (cr);
      break;

    default:
      break;
    }
}
