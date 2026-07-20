#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeActionBar — old-school Aqua footer with right-aligned Cancel + Apply.
 *
 * The classic Mac / GNOME settings pattern: edits stage in the UI and only
 * commit when the user clicks Apply; Cancel reverts to the last-saved state.
 *
 * Connect the two returned buttons' "clicked" signals, and call
 * ooze_action_bar_set_dirty() as edits are made so the footer enables/disables
 * itself.
 */
GtkWidget *ooze_action_bar_new (GtkWidget **out_cancel,
                                GtkWidget **out_apply);

/* Shared CSS for .ooze-action-bar. */
void ooze_action_bar_ensure_css (void);

/* Enable Apply + Cancel when there are unsaved changes, disable otherwise. */
void ooze_action_bar_set_dirty (GtkWidget *bar,
                                gboolean   dirty);

G_END_DECLS
