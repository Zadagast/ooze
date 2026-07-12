#pragma once

#include <gio/gio.h>
#include <meta/display.h>
#include <meta/window.h>

#include "my-aqua-menu.h"

G_BEGIN_DECLS

#define MY_GLOBAL_MENU_MAX_TOP 12
#define MY_MENU_APP_ACTION_BASE 5000

typedef struct _MyGlobalMenu MyGlobalMenu;

typedef void (*MyGlobalMenuChangedFunc) (gpointer user_data);

MyGlobalMenu *my_global_menu_new (MetaDisplay *display);

void my_global_menu_free (MyGlobalMenu *menu);

void my_global_menu_set_changed_callback (MyGlobalMenu           *menu,
                                          MyGlobalMenuChangedFunc callback,
                                          gpointer                user_data);

/* Sync to the focused window (or clear when none / no GTK menubar). */
void my_global_menu_sync_focus (MyGlobalMenu *menu);

gboolean my_global_menu_has_app_menu (MyGlobalMenu *menu);

guint my_global_menu_get_n_top (MyGlobalMenu *menu);

/* Borrowed label valid until next sync/items-changed. */
const char *my_global_menu_get_top_label (MyGlobalMenu *menu,
                                          guint         index);

/*
 * Build popup rows for top-level section `index`.
 * Entries are owned by MyGlobalMenu until the next call / sync.
 * action_id values are MY_MENU_APP_ACTION_BASE + N for app actions.
 */
gboolean my_global_menu_fill_entries (MyGlobalMenu     *menu,
                                      guint             top_index,
                                      MyAquaMenuEntry **entries_out,
                                      gsize            *n_entries_out);

/* Activate a previously filled app action_id. */
void my_global_menu_activate (MyGlobalMenu *menu,
                              int           action_id);

G_END_DECLS
