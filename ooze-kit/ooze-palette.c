#include "ooze-palette.h"
#include "ooze-theme.h"

/* ── Light palette ──────────────────────────────────────────────────────────
 *
 *  Header / Toolbar / Sidebar share one aluminum band so pinlines read as
 *  continuous chrome wrapping the white content view (classic Finder).
 *
 *  Header / Toolbar / Sidebar / Statusbar  #f0f0f0 — one cloth, flush joins
 * ────────────────────────────────────────────────────────────────────────── */
static const OozePalette LIGHT = {
  .header_r    = 0.941, .header_g    = 0.941, .header_b    = 0.941,
  .toolbar_r   = 0.941, .toolbar_g   = 0.941, .toolbar_b   = 0.941,
  .sidebar_r   = 0.941, .sidebar_g   = 0.941, .sidebar_b   = 0.941,
  /* Same aluminum as header/toolbar so the footer reads as continuous chrome. */
  .statusbar_r = 0.941, .statusbar_g = 0.941, .statusbar_b = 0.941,

  .pinstripe_alpha  = 0.06,
  .pinstripe_stride = 4, /* OOZE_PIN_STRIDE — keep in sync with aqua-chrome.h */

  .sep_alpha            = 0.16,
  .sep_highlight_alpha  = 0.40,

  .btn_hover_a  = 0.07,
  .btn_active_a = 0.18,

  .accent_r = 0.157, .accent_g = 0.408, .accent_b = 0.784,

  .dark = FALSE,
};

/* ── Dark palette ───────────────────────────────────────────────────────────
 *
 *  Header / Toolbar / Sidebar stay in one charcoal family for the same
 *  continuous-chrome read as light mode.
 * ────────────────────────────────────────────────────────────────────────── */
static const OozePalette DARK = {
  .header_r    = 0.196, .header_g = 0.196, .header_b    = 0.200,
  .toolbar_r   = 0.196, .toolbar_g = 0.196, .toolbar_b   = 0.200,
  .sidebar_r   = 0.196, .sidebar_g = 0.196, .sidebar_b   = 0.200,
  .statusbar_r = 0.196, .statusbar_g = 0.196, .statusbar_b = 0.200,

  .pinstripe_alpha  = 0.12,
  .pinstripe_stride = 4, /* OOZE_PIN_STRIDE — keep in sync with aqua-chrome.h */

  /* Soft hairlines — strong grooves break continuous Ooze Gel pinlines. */
  .sep_alpha            = 0.22,
  .sep_highlight_alpha  = 0.10,

  .btn_hover_a  = 0.10,
  .btn_active_a = 0.28,

  .accent_r = 0.157, .accent_g = 0.408, .accent_b = 0.784,

  .dark = TRUE,
};

const OozePalette *
ooze_palette_current (void)
{
  return ooze_theme_is_dark () ? &DARK : &LIGHT;
}
