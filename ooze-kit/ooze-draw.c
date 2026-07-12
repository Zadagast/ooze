#include "ooze-draw.h"

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

/* ── Public API ─────────────────────────────────────────────────────────── */

void
ooze_draw_surface (cairo_t          *cr,
                   int               w,
                   int               h,
                   double            r,
                   double            g,
                   double            b,
                   const OozePalette *p)
{
  /* Base fill */
  cairo_set_source_rgb (cr, r, g, b);
  cairo_rectangle (cr, 0, 0, w, h);
  cairo_fill (cr);

  /* Pinstripe: one 1 px tinted line every stride rows.
   * Dark mode → white highlights on charcoal.
   * Light mode → dark shadows on pale gray (reversed for visibility). */
  if (p->pinstripe_alpha > 0.001 && p->pinstripe_stride > 1)
    {
      double pr = p->dark ? 1.0 : 0.0;
      double pg = p->dark ? 1.0 : 0.0;
      double pb = p->dark ? 1.0 : 0.0;
      cairo_set_source_rgba (cr, pr, pg, pb, p->pinstripe_alpha);
      for (int py = 0; py < h; py += p->pinstripe_stride)
        {
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
  double hi_r = p->dark ? 1.0 : 1.0;
  double hi_g = p->dark ? 1.0 : 1.0;
  double hi_b = p->dark ? 1.0 : 1.0;
  double lo_a = p->sep_alpha;
  double hi_a = p->sep_highlight_alpha;

  switch (side)
    {
    case OOZE_SIDE_BOTTOM:
      /* Groove sits on the bottom edge; highlight just above it. */
      cairo_set_source_rgba (cr, hi_r, hi_g, hi_b, hi_a);
      cairo_rectangle (cr, 0, h - 2, w, 1);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, h - 1, w, 1);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_TOP:
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, 0, w, 1);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, hi_r, hi_g, hi_b, hi_a);
      cairo_rectangle (cr, 0, 1, w, 1);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_RIGHT:
      cairo_set_source_rgba (cr, hi_r, hi_g, hi_b, hi_a);
      cairo_rectangle (cr, w - 2, 0, 1, h);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, w - 1, 0, 1, h);
      cairo_fill (cr);
      break;

    case OOZE_SIDE_LEFT:
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, lo_a);
      cairo_rectangle (cr, 0, 0, 1, h);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, hi_r, hi_g, hi_b, hi_a);
      cairo_rectangle (cr, 1, 0, 1, h);
      cairo_fill (cr);
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
  /* Radius scales slightly with height so compact tiles stay round. */
  const double R = CLAMP (MIN (8.0, h * 0.28), 5.0, 8.0);

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
          ? (p->dark ? 0.58 : 0.22)
          : (p->dark ? 0.42 : 0.16);
        const double smoke_a_mid = pressed
          ? (p->dark ? 0.44 : 0.16)
          : (p->dark ? 0.30 : 0.11);
        const double smoke_a_bot = pressed
          ? (p->dark ? 0.34 : 0.12)
          : (p->dark ? 0.22 : 0.08);
        const double border_hi = p->dark ? 0.34 : 0.72;
        const double border_lo = p->dark ? 0.28 : 0.30;
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
                                               1.0, 1.0, 1.0, smoke_a_top + 0.28);
            cairo_pattern_add_color_stop_rgba (grad, 0.5,
                                               glass_r, glass_g, glass_b, smoke_a_mid + 0.18);
            cairo_pattern_add_color_stop_rgba (grad, 1.0,
                                               0.78, 0.80, 0.84, smoke_a_bot + 0.14);
            cairo_set_source (cr, grad);
            cairo_fill_preserve (cr);
            cairo_pattern_destroy (grad);

            cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, pressed ? 0.10 : 0.07);
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
        rounded_rect (cr, x + 2.0, y + 2.0,
                      w - 4.0, h * 0.40,
                      MAX (R - 2.5, 2.0));
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, p->dark ? 0.16 : 0.45);
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
