#pragma once

#include <glib.h>

/*
 * OozePalette — single source of truth for every colour used by OozeKit
 * widgets.  Populate with ooze_palette_current() which reads the live
 * AdwStyleManager to return a pointer to either the built-in light or dark
 * constant table.  All draw helpers take a const OozePalette*.
 */
typedef struct
{
  /* ── Surface fills ─────────────────────────────────────────────────────── */
  double header_r,    header_g,    header_b;
  double toolbar_r,   toolbar_g,   toolbar_b;
  double sidebar_r,   sidebar_g,   sidebar_b;
  double statusbar_r, statusbar_g, statusbar_b;

  /* ── Pinstripe overlay ──────────────────────────────────────────────────
   * One 1 px white line every `stride` rows gives a satin-weave texture.
   * stride = 4  ⟹  one highlight, three gap rows. */
  double pinstripe_alpha;
  int    pinstripe_stride;

  /* ── Hairline separators (Aqua pinlines) ────────────────────────────────
   * Groove: dark line + light highlight so chrome edges read as continuous
   * brushed metal rather than stacked cards. */
  double sep_alpha;
  double sep_highlight_alpha;

  /* ── Button states ──────────────────────────────────────────────────────
   * Hover and active tints are RGBA because they overlay variable surfaces. */
  double btn_hover_a;       /* white (dark) or black (light) tint alpha */
  double btn_active_a;      /* accent tint alpha for toggled-on buttons  */

  /* ── Accent (Ooze blue) ─────────────────────────────────────────────── */
  double accent_r, accent_g, accent_b;

  /* ── Convenience ────────────────────────────────────────────────────── */
  gboolean dark;
} OozePalette;

/*
 * Returns a pointer to the static palette matching the current
 * AdwStyleManager dark/light setting.  Never NULL.
 */
const OozePalette *ooze_palette_current (void);
