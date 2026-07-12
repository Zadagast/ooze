#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeToolbar — MAIN BAR (OozeKit strip directly under the Ooze Gel title bar).
 *
 * Shares the Ooze Gel pinline cloth. Layout metrics live in ooze-toolbar.c /
 * ooze-button.c / aqua-chrome.h so every app shares one rhythm:
 *
 *   • TOOLBAR icons: OOZE_ICON_SIZE_TOOLBAR (40)
 *   • Height:        content-driven (no forced height); OOZE_TOOLBAR_HEIGHT
 *                     is a nominal reference (~96) for docs, not enforced
 *   • Padding:       Adwaita .toolbar rhythm — padding 6px, border-spacing 6px
 *                     (libadwaita stylesheet); glass rim needs OOZE_BTN_PAD_* ≥ 6
 *   • Glass outset / inset: OOZE_BTN_ICON_OUTSET / OOZE_BTN_PAD_* (in button.c)
 *   • Equal tile min-width; spacer before trailing search
 *   • Flush joins — no outer margins (pinlines must continue)
 *   • Stripe phase via ooze_stripe_origin_y() on the Ooze Gel grid
 *
 * Compose with:
 *   ooze_toolbar_new ()
 *   ooze_toolbar_add_group () + ooze_button_new_toolbar ()
 *   ooze_toolbar_add_separator ()
 *   ooze_toolbar_add_spacer ()
 *   trailing widget + CSS class "ooze-toolbar-search" when appropriate
 */

GtkWidget *ooze_toolbar_new (void);

/* Shared CSS for .ooze-toolbar / .ooze-toolbar-btn / launcher tiles */
void ooze_toolbar_ensure_css (void);

/* Nested HBox for a cluster of toolbar buttons (nav / view / places). */
GtkWidget *ooze_toolbar_add_group (GtkWidget *toolbar);

/* Vertical hairline between groups. */
GtkWidget *ooze_toolbar_add_separator (GtkWidget *toolbar);

/* Expanding spacer (e.g. before Search). */
GtkWidget *ooze_toolbar_add_spacer (GtkWidget *toolbar);

G_END_DECLS
