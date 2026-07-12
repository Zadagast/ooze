#pragma once

#include "ooze-palette.h"

#include <cairo/cairo.h>

/*
 * ooze-draw — stateless Cairo primitives for OozeKit surfaces and buttons.
 *
 * Every function takes a fully-populated OozePalette so callers only need one
 * call to ooze_palette_current() per snapshot pass.  Functions do NOT call
 * cairo_destroy(); the caller owns the Cairo context.
 */

/* Which edge a separator hairline sits on. */
typedef enum
{
  OOZE_SIDE_TOP,
  OOZE_SIDE_BOTTOM,
  OOZE_SIDE_LEFT,
  OOZE_SIDE_RIGHT,
} OozeSide;

/* Visual state for ooze_draw_button_bg(). */
typedef enum
{
  OOZE_BTN_NORMAL  = 0,   /* no fill — fully transparent */
  OOZE_BTN_HOVER   = 1,   /* subtle tint */
  OOZE_BTN_ACTIVE  = 2,   /* accent tint (toggled-on) */
  OOZE_BTN_PRESSED = 3,   /* solid accent + dark border */
} OozeBtnState;

/*
 * ooze_draw_surface – flat fill + pinstripe overlay.
 *
 * Fills the rectangle [0, 0, w, h] with the given RGB colour and then
 * overlays one bright 1 px stripe every palette->pinstripe_stride rows.
 * No separator is drawn; call ooze_draw_separator() separately.
 */
void ooze_draw_surface   (cairo_t          *cr,
                          int               w,
                          int               h,
                          double            r,
                          double            g,
                          double            b,
                          const OozePalette *p);

/*
 * ooze_draw_separator – 1 px hairline on one edge.
 *
 * Colour is always (0, 0, 0) × p->sep_alpha.
 */
void ooze_draw_separator (cairo_t          *cr,
                          int               w,
                          int               h,
                          OozeSide          side,
                          const OozePalette *p);

/*
 * ooze_draw_button_bg – rounded-rect button fill.
 *
 * Draws inside a 2 px inset margin (M) so the fill doesn't clip at the
 * widget boundary.  OOZE_BTN_NORMAL is a no-op; callers may skip the
 * Cairo context creation in that case.
 */
void ooze_draw_button_bg (cairo_t          *cr,
                          int               w,
                          int               h,
                          OozeBtnState      state,
                          const OozePalette *p);
