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
                     int               w,
                     int               h,
                     OozeBtnState      state,
                     const OozePalette *p)
{
  /* 2 px inset so the fill doesn't touch widget edges */
  const double M = 2.0;
  const double R = 6.0;

  if (state == OOZE_BTN_NORMAL)
    return;

  switch (state)
    {
    case OOZE_BTN_PRESSED:
      cairo_set_source_rgba (cr,
                             p->accent_r, p->accent_g, p->accent_b, 1.0);
      rounded_rect (cr, M, M, w - M * 2.0, h - M * 2.0, R);
      cairo_fill (cr);

      /* crisp border around the blue pill */
      rounded_rect (cr, M + 0.5, M + 0.5,
                    w - (M + 0.5) * 2.0, h - (M + 0.5) * 2.0, R);
      cairo_set_source_rgba (cr,
                             p->accent_r * 0.55,
                             p->accent_g * 0.55,
                             p->accent_b * 0.55,
                             0.60);
      cairo_set_line_width (cr, 1.0);
      cairo_stroke (cr);
      break;

    case OOZE_BTN_ACTIVE:
      rounded_rect (cr, M, M, w - M * 2.0, h - M * 2.0, R);
      cairo_set_source_rgba (cr,
                             p->accent_r, p->accent_g, p->accent_b,
                             p->btn_active_a);
      cairo_fill (cr);
      break;

    case OOZE_BTN_HOVER:
      rounded_rect (cr, M, M, w - M * 2.0, h - M * 2.0, R);
      /* Light on dark, dark on light */
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
