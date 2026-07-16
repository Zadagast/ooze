#pragma once

#include "ooze-plugin.h"

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  OOZE_MENU_FILE_NEW_SPOT = 1,
  OOZE_MENU_FILE_CLOSE,
  OOZE_MENU_EDIT_UNDO,
  OOZE_MENU_EDIT_CUT,
  OOZE_MENU_EDIT_COPY,
  OOZE_MENU_EDIT_PASTE,
  OOZE_MENU_EDIT_SELECT_ALL,
  OOZE_MENU_VIEW_ICONS,
  OOZE_MENU_VIEW_LIST,
  OOZE_MENU_VIEW_COLUMNS,
  OOZE_MENU_GO_COMPUTER,
  OOZE_MENU_GO_HOME,
  OOZE_MENU_GO_DESKTOP,
  OOZE_MENU_GO_DOCUMENTS,
  OOZE_MENU_GO_DOWNLOADS,
  OOZE_MENU_GO_APPLICATIONS,
  OOZE_MENU_WINDOW_MINIMIZE,
  OOZE_MENU_WINDOW_ZOOM,
  OOZE_MENU_WINDOW_BRING_ALL,
  OOZE_MENU_WINDOW_FOCUS_BASE = 100,
  OOZE_MENU_HELP_ABOUT        = 200,
  OOZE_MENU_APP_HIDE          = 300,
  OOZE_MENU_APP_QUIT,
  OOZE_MENU_OOZE_RESTART      = 1000,
  OOZE_MENU_OOZE_SHUTDOWN,
  OOZE_MENU_OOZE_LOGOUT,
  OOZE_MENU_OOZE_LOCK,
  OOZE_MENU_OOZE_APPEARANCE,
  OOZE_MENU_OOZE_SUSPEND,
} OozeMenuAction;

/* Shell menu action labels — null-terminated array of strings shown in the
 * menu bar when no app menu is bound.  Index matches action_id above. */
extern const char *const ooze_shell_menu_bar_items[];
extern const gsize ooze_shell_menu_bar_n_items;

/*
 * Action callback registered with OozeAquaMenu.  Dispatches OozeMenuAction
 * values to window operations, dock launches, and session management.
 * @user_data: the owning OozePlugin cast to gpointer.
 */
void ooze_shell_menu_action (gpointer user_data, int action_id);

G_END_DECLS
