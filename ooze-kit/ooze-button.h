#pragma once

#include "ooze-icons.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeButton — GtkButton subclass with the Ooze hover/press/active look.
 *
 * RULE (all Ooze apps): toolbar controls look like Spot’s Computer / Home /
 * Favorites / Applications tiles:
 *   • full-color (non-symbolic) theme icon
 *   • caption label centered under the icon
 * Do not ship icon-only symbolic toolbar pills for app controls.
 *
 *   OOZE_BUTTON_TOOLBAR  – toolbar / nav / settings grid controls
 *   OOZE_BUTTON_PUSH     – dialogs and other push buttons
 *
 * Exclusive toggles (e.g. Spot Grid / Columns): prefer
 * ooze_button_set_exclusive() so exactly one peer shows the glass plate.
 */
typedef enum
{
  OOZE_BUTTON_TOOLBAR,
  OOZE_BUTTON_PUSH,
} OozeButtonKind;

#define OOZE_TYPE_BUTTON (ooze_button_get_type ())
G_DECLARE_FINAL_TYPE (OozeButton, ooze_button, OOZE, BUTTON, GtkButton)

/* Blank button (rarely needed — prefer ooze_button_new_labeled). */
GtkWidget *ooze_button_new (OozeButtonKind kind);

/*
 * Canonical chrome button: color icon + label underneath.
 * icon_px <= 0 defaults to OOZE_ICON_SIZE_TOOLBAR (24).
 * Symbolic icon names are used only when no color icon exists.
 */
GtkWidget *ooze_button_new_labeled (OozeButtonKind      kind,
                                    const char * const *icon_names,
                                    int                 icon_px,
                                    const char         *label,
                                    const char         *tooltip);

/* Same as ooze_button_new_labeled() with OOZE_BUTTON_TOOLBAR + 24px icons. */
GtkWidget *ooze_button_new_toolbar (const char * const *icon_names,
                                    const char         *label,
                                    const char         *tooltip);

/* Deprecated alias — still creates a labeled color button (label required). */
GtkWidget *ooze_button_new_icon (OozeButtonKind      kind,
                                 const char * const *icon_names);

/*
 * Sticky toggle state (CSS class "active" + redraw).
 * Prefer ooze_button_set_exclusive() for radio-style toolbar peers.
 */
void     ooze_button_set_toggled (GtkWidget *button, gboolean toggled);
gboolean ooze_button_get_toggled (GtkWidget *button);

/* Sets peers[active] toggled; all other peers off. active >= n_peers clears all. */
void ooze_button_set_exclusive (GtkWidget **peers,
                                gsize       n_peers,
                                gsize       active);

G_END_DECLS
