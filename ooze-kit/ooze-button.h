#pragma once

#include "ooze-button.h"
#include "ooze-icons.h"

/*
 * OozeButton — GtkButton subclass with the Ooze hover/press/active look.
 *
 * RULE (all Ooze apps): chrome buttons look like Spot’s Computer / Home /
 * Favorites / Applications controls:
 *   • full-color (non-symbolic) theme icon
 *   • caption label centered under the icon
 * Do not ship icon-only symbolic toolbar pills for app chrome.
 *
 *   OOZE_BUTTON_TOOLBAR  – toolbar / nav / settings grid chrome
 *   OOZE_BUTTON_PUSH     – dialogs and other push buttons
 *
 * Toggle: add/remove the "active" CSS class for the pressed accent tint.
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
 * icon_px <= 0 defaults to OOZE_ICON_SIZE_TOOLBAR (32).
 * Symbolic icon names are used only when no color icon exists.
 */
GtkWidget *ooze_button_new_labeled (OozeButtonKind      kind,
                                    const char * const *icon_names,
                                    int                 icon_px,
                                    const char         *label,
                                    const char         *tooltip);

/* Same as ooze_button_new_labeled() with OOZE_BUTTON_TOOLBAR + 32px icons. */
GtkWidget *ooze_button_new_toolbar (const char * const *icon_names,
                                    const char         *label,
                                    const char         *tooltip);

/* Deprecated alias — still creates a labeled color button (label required). */
GtkWidget *ooze_button_new_icon (OozeButtonKind      kind,
                                 const char * const *icon_names);
