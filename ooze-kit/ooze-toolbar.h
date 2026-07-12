#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeToolbar — Spot-style chrome strip used by Spot, System Settings, etc.
 *
 * Returns an OozeSurface (TOOLBAR) with shared CSS. Append buttons created
 * with ooze_button_new_toolbar() / ooze_button_new_labeled() only — always
 * full-color icon + caption underneath (never symbolic icon-only pills).
 */

GtkWidget *ooze_toolbar_new (void);

/* Shared CSS for .ooze-toolbar / .ooze-toolbar-icon-btn / .ooze-settings-tile */
void ooze_toolbar_ensure_css (void);

G_END_DECLS
