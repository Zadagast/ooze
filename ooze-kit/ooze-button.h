#pragma once

#include <gtk/gtk.h>

/*
 * OozeButton — GtkButton subclass with the Ooze hover/press/active look.
 *
 * The button's own CSS background/border/shadow is cleared; all interactive
 * states are painted in Cairo by ooze_draw_button_bg().
 *
 *   OOZE_BUTTON_TOOLBAR  – icon-only flat pill (toolbar, nav strip)
 *   OOZE_BUTTON_PUSH     – labelled push button (dialogs, non-toolbar uses)
 *
 * Toggle behaviour: call ooze_button_set_active() or add/remove the "active"
 * CSS class to flip the toggled-on accent tint.
 */
typedef enum
{
  OOZE_BUTTON_TOOLBAR,
  OOZE_BUTTON_PUSH,
} OozeButtonKind;

#define OOZE_TYPE_BUTTON (ooze_button_get_type ())
G_DECLARE_FINAL_TYPE (OozeButton, ooze_button, OOZE, BUTTON, GtkButton)

/* Create a blank OozeButton. */
GtkWidget *ooze_button_new       (OozeButtonKind kind);

/* Create an OozeButton pre-loaded with the first icon found in icon_names
 * (NULL-terminated array, searched in order against the current icon theme). */
GtkWidget *ooze_button_new_icon  (OozeButtonKind      kind,
                                  const char * const *icon_names);
