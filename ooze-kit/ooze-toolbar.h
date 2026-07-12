#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeToolbar — Spot-style chrome strip used by Spot, System Settings, etc.
 *
 * Returns an OozeSurface (TOOLBAR) with shared CSS. Append buttons created
 * with ooze_button_new_toolbar() / ooze_button_new_labeled() only — always
 * full-color icon + caption underneath (never symbolic icon-only pills).
 *
 * Prefer the helpers below so group spacing stays consistent across apps.
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
