#include "ooze-palette.h"

#include <adwaita.h>

/* ── Light palette ──────────────────────────────────────────────────────────
 *
 *  Header     #efefef  neutral near-white so traffic lights read on light bg
 *  Toolbar    #f5f5f5  slightly lighter (top surface, most exposed)
 *  Sidebar    #ebebeb  a touch darker to visually separate content
 *  Statusbar  #f0f0f0  between toolbar and sidebar
 *
 *  Pinstripes: stride-4, alpha 0.30 — visible satin texture on pale surfaces
 *  Separator:  alpha 0.13 — very subtle hairline
 * ────────────────────────────────────────────────────────────────────────── */
static const OozePalette LIGHT = {
  .header_r    = 0.937, .header_g    = 0.937, .header_b    = 0.941,
  .toolbar_r   = 0.961, .toolbar_g   = 0.961, .toolbar_b   = 0.961,
  .sidebar_r   = 0.922, .sidebar_g   = 0.922, .sidebar_b   = 0.922,
  .statusbar_r = 0.941, .statusbar_g = 0.941, .statusbar_b = 0.941,

  /* dark lines on pale surfaces — 0.08 alpha gives a subtle satin texture */
  .pinstripe_alpha  = 0.08,
  .pinstripe_stride = 4,

  .sep_alpha   = 0.13,

  /* button tints: faint dark overlay */
  .btn_hover_a  = 0.07,
  .btn_active_a = 0.18,

  /* Ooze blue */
  .accent_r = 0.157, .accent_g = 0.408, .accent_b = 0.784,

  .dark = FALSE,
};

/* ── Dark palette ───────────────────────────────────────────────────────────
 *
 *  Header     #313133  Ooze signature dark charcoal
 *  Toolbar    #353537  step lighter so toolbar floats above header
 *  Sidebar    #2f2f30  step darker than toolbar — inset shadow feel
 *  Statusbar  #323234  between header and toolbar
 *
 *  Pinstripes: stride-4, alpha 0.13 — ghost highlight on charcoal
 *  Separator:  alpha 0.40 — stronger contrast against dark surfaces
 * ────────────────────────────────────────────────────────────────────────── */
static const OozePalette DARK = {
  .header_r    = 0.192, .header_g = 0.192, .header_b    = 0.196,
  .toolbar_r   = 0.208, .toolbar_g = 0.208, .toolbar_b   = 0.212,
  .sidebar_r   = 0.184, .sidebar_g = 0.184, .sidebar_b   = 0.188,
  .statusbar_r = 0.196, .statusbar_g = 0.196, .statusbar_b = 0.200,

  .pinstripe_alpha  = 0.13,
  .pinstripe_stride = 4,

  .sep_alpha   = 0.40,

  /* button tints: faint white overlay */
  .btn_hover_a  = 0.10,
  .btn_active_a = 0.28,

  /* Ooze blue */
  .accent_r = 0.157, .accent_g = 0.408, .accent_b = 0.784,

  .dark = TRUE,
};

const OozePalette *
ooze_palette_current (void)
{
  return adw_style_manager_get_dark (adw_style_manager_get_default ())
         ? &DARK : &LIGHT;
}
