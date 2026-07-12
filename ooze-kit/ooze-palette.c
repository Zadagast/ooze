#include "ooze-palette.h"

#include <adwaita.h>

/* ── Light palette ──────────────────────────────────────────────────────────
 *
 *  Header / Toolbar / Sidebar share one aluminum band so pinlines read as
 *  continuous chrome wrapping the white content view (classic Finder).
 *
 *  Header     #f0f0f0
 *  Toolbar    #f0f0f0  same — header→toolbar is one surface split by a hairline
 *  Sidebar    #ececec  tiny step so places still separate from the view
 *  Statusbar  #f0f0f0
 * ────────────────────────────────────────────────────────────────────────── */
static const OozePalette LIGHT = {
  .header_r    = 0.941, .header_g    = 0.941, .header_b    = 0.941,
  .toolbar_r   = 0.941, .toolbar_g   = 0.941, .toolbar_b   = 0.941,
  .sidebar_r   = 0.925, .sidebar_g   = 0.925, .sidebar_b   = 0.925,
  /* Same aluminum as header/toolbar so the footer reads as continuous chrome. */
  .statusbar_r = 0.941, .statusbar_g = 0.941, .statusbar_b = 0.941,

  .pinstripe_alpha  = 0.06,
  .pinstripe_stride = 4,

  .sep_alpha            = 0.18,
  .sep_highlight_alpha  = 0.55,

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
  .sidebar_r   = 0.180, .sidebar_g = 0.180, .sidebar_b   = 0.184,
  .statusbar_r = 0.196, .statusbar_g = 0.196, .statusbar_b = 0.200,

  .pinstripe_alpha  = 0.12,
  .pinstripe_stride = 4,

  .sep_alpha            = 0.45,
  .sep_highlight_alpha  = 0.14,

  .btn_hover_a  = 0.10,
  .btn_active_a = 0.28,

  .accent_r = 0.157, .accent_g = 0.408, .accent_b = 0.784,

  .dark = TRUE,
};

const OozePalette *
ooze_palette_current (void)
{
  return adw_style_manager_get_dark (adw_style_manager_get_default ())
         ? &DARK : &LIGHT;
}
