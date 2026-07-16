#pragma once

#include <gio/gio.h>
#include <meta/display.h>
#include <meta/window.h>

#include "ooze-aqua-menu.h"

G_BEGIN_DECLS

#define OOZE_GLOBAL_MENU_MAX_TOP 12
#define OOZE_MENU_APP_ACTION_BASE 5000

typedef struct _OozeGlobalMenu OozeGlobalMenu;
typedef struct _OozeAppmenuWayland OozeAppmenuWayland;

typedef void (*OozeGlobalMenuChangedFunc) (gpointer user_data);

OozeGlobalMenu *ooze_global_menu_new (MetaDisplay *display);

void ooze_global_menu_free (OozeGlobalMenu *menu);

/* Wayland org_kde_kwin_appmenu registry (non-owning; plugin owns). */
void ooze_global_menu_set_wayland_appmenu (OozeGlobalMenu     *menu,
                                           OozeAppmenuWayland *wl_appmenu);

void ooze_global_menu_set_changed_callback (OozeGlobalMenu           *menu,
                                          OozeGlobalMenuChangedFunc callback,
                                          gpointer                user_data);

/* Sync to the focused window (or clear when none / no GTK menubar). */
void ooze_global_menu_sync_focus (OozeGlobalMenu *menu);

/* Re-read GTK shell props for the focused / last-bound window before opening. */
void ooze_global_menu_prepare_for_open (OozeGlobalMenu *menu);

/*
 * If fill_entries returned FALSE because a D-Bus submenu was still empty,
 * this returns TRUE once with the top index that should be reopened.
 */
gboolean ooze_global_menu_take_pending_top (OozeGlobalMenu *menu,
                                          guint        *top_index_out);

void ooze_global_menu_discard_pending (OozeGlobalMenu *menu);

gboolean ooze_global_menu_has_app_menu (OozeGlobalMenu *menu);

/* TRUE when bound via AppMenu registrar dbusmenu (Inkscape / GTK3). */
gboolean ooze_global_menu_has_dbusmenu (OozeGlobalMenu *menu);

/*
 * Non-NULL when focus is a classic GtkMenuBar app on native Wayland
 * (e.g. Inkscape). Panel may show this one-line hint; global menu cannot
 * bind until relaunched with GDK_BACKEND=x11 via Spot/Command/dock.
 */
const char *ooze_global_menu_get_x11_launch_hint (OozeGlobalMenu *menu);

/*
 * TRUE when the shell Spot/Finder stub menus are appropriate (desktop /
 * Spot / no foreign app focused). Foreign apps without an export get an
 * empty bar — never Spot's File/Go entries.
 */
gboolean ooze_global_menu_wants_shell_stubs (OozeGlobalMenu *menu);

/*
 * Foreign window in focus (or last watched/bound) that exports no menu.
 * Non-NULL means the panel should synthesize a baseline app/Window menu
 * for it instead of showing an empty bar.
 */
MetaWindow *ooze_global_menu_get_fallback_window (OozeGlobalMenu *menu);

guint ooze_global_menu_get_n_top (OozeGlobalMenu *menu);

/* Borrowed label valid until next sync/items-changed. */
const char *ooze_global_menu_get_top_label (OozeGlobalMenu *menu,
                                          guint         index);

/*
 * Build popup rows for top-level section `index`.
 * Entries are owned by OozeGlobalMenu until the next call / sync.
 * action_id values are OOZE_MENU_APP_ACTION_BASE + N for app actions.
 */
gboolean ooze_global_menu_fill_entries (OozeGlobalMenu     *menu,
                                      guint             top_index,
                                      OozeAquaMenuEntry **entries_out,
                                      gsize            *n_entries_out);

/* Activate a previously filled app action_id. */
void ooze_global_menu_activate (OozeGlobalMenu *menu,
                              int           action_id);

/* Last window that owns the bound app menu (may be minimized). */
MetaWindow *ooze_global_menu_get_bound_window (OozeGlobalMenu *menu);

G_END_DECLS
