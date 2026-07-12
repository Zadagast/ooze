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
  OOZE_BTN_ACTIVE  = 2,   /* clear dock-like glass plate (toggled-on) */
  OOZE_BTN_PRESSED = 3,   /* denser glass plate while pressed */
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
 * ooze_draw_separator – Aqua pinline (dark groove + light highlight).
 *
 * Drawn just inside the edge so header/toolbar/sidebar joins read as one
 * continuous chrome band instead of stacked cards with a hard drop.
 */
void ooze_draw_separator (cairo_t          *cr,
                          int               w,
                          int               h,
                          OozeSide          side,
                          const OozePalette *p);

/*
 * ooze_draw_button_bg – rounded glass / hover fill at an explicit rect.
 *
 * Callers pass the plate rectangle in widget coordinates. OozeButton passes
 * its allocation inset by a fixed edge so the glass always frames the full
 * control. OOZE_BTN_NORMAL is a no-op.
 */
void ooze_draw_button_bg (cairo_t          *cr,
                          double            x,
                          double            y,
                          double            w,
                          double            h,
                          OozeBtnState      state,
                          const OozePalette *p);
