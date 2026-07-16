#include "ooze-global-menu.h"
#include "ooze-appmenu-wayland.h"
#include "ooze-shared-appmenu.h"
#include "ooze-dbusmenu.h"
#include "ooze-stall.h"

#include <clutter/clutter.h>
#include <meta/display.h>
#include <string.h>

/* Exported by libmutter but not always in public headers. */
gulong meta_window_x11_get_xwindow (MetaWindow *window);
gulong meta_window_x11_get_toplevel_xwindow (MetaWindow *window);

#define OOZE_REGISTRAR_RETRY_DEFAULT 20   /* ~5s */
#define OOZE_REGISTRAR_RETRY_INKSCAPE 60  /* ~15s — lazy dbusmenu tops */
/* Coalesce Alt-Tab focus chatter before GDBus bind / panel emit. */
#define OOZE_FOCUS_SYNC_DEBOUNCE_MS 60

typedef struct
{
  char     *action;
  GVariant *target;
} OozeGlobalMenuAction;

struct _OozeGlobalMenu
{
  MetaDisplay *display;
  OozeAppmenuWayland *wl_appmenu; /* non-owning; plugin owns */
  MetaWindow  *bound_window; /* non-owning; cleared on rebind */
  MetaWindow  *watched_window; /* non-owning; GTK shell prop watch */

  char *bus_name;
  char *menubar_path;
  char *app_path;
  char *win_path;

  GDBusConnection *session;
  GMenuModel      *menubar;
  GActionGroup    *app_actions;
  GActionGroup    *win_actions;
  OozeDbusmenu      *dbusmenu;
  OozeDbusmenuItem  *dbus_tops;
  gsize            n_dbus_tops;

  gulong focus_handler;
  gulong items_handler;
  gulong watch_bus_handler;
  gulong watch_menubar_handler;
  gulong watch_appmenu_handler;
  gulong watch_app_handler;
  gulong watch_win_handler;
  gulong pending_submenu_handler;
  guint  registrar_signal_id;
  GMenuModel *pending_submenu;
  guint       pending_top_index;
  gboolean    pending_top_valid;
  guint       suppress_changed;
  guint       prime_idle;
  guint       focus_sync_idle;
  guint       activate_idle;
  guint       registrar_retry_id;
  guint       registrar_retry_left;
  MetaWindow *registrar_retry_window; /* non-owning */

  GCancellable *cancellable;       /* whole-object lifetime */
  GCancellable *bind_cancellable;  /* in-flight registrar query */
  MetaWindow   *pending_bind_window; /* weak pointer */
  gboolean      registrar_service_requested;

  /* Strong refs so GDBusMenuModel submenu Start() stays alive. */
  GMenuModel *submenu_models[OOZE_GLOBAL_MENU_MAX_TOP];
  gulong      submenu_handlers[OOZE_GLOBAL_MENU_MAX_TOP];

  OozeGlobalMenuChangedFunc changed_cb;
  gpointer                changed_data;

  OozeAquaMenuEntry    *entries;
  gsize               n_entries;
  OozeGlobalMenuAction *actions;
  gsize               n_actions;

  /* Deferred activate payload (panel focus restore needs a beat). */
  char     *pending_action;
  GVariant *pending_target;

  /* Shown in the panel when a classic GtkMenuBar app is Wayland-native. */
  char *x11_launch_hint;
};

static void ooze_global_menu_clear_proxy (OozeGlobalMenu *menu);
static void ooze_global_menu_clear_entries (OozeGlobalMenu *menu);
static void ooze_global_menu_clear_pending_submenu (OozeGlobalMenu *menu);
static void ooze_global_menu_clear_submenu_cache (OozeGlobalMenu *menu);
static void ooze_global_menu_emit_changed (OozeGlobalMenu *menu);
static void ooze_global_menu_watch_window (OozeGlobalMenu *menu,
                                         MetaWindow   *window);
static void ooze_global_menu_bind_window (OozeGlobalMenu *menu,
                                        MetaWindow   *window);
static void ooze_global_menu_prime_submenus (OozeGlobalMenu *menu);
static void ooze_global_menu_schedule_prime (OozeGlobalMenu *menu);
static void ooze_global_menu_schedule_focus_sync (OozeGlobalMenu *menu);
static void ooze_global_menu_cancel_focus_sync (OozeGlobalMenu *menu);
static void ooze_global_menu_flush_focus_sync (OozeGlobalMenu *menu);
static void ooze_global_menu_cancel_registrar_retry (OozeGlobalMenu *menu);
static void ooze_global_menu_schedule_registrar_retry (OozeGlobalMenu *menu,
                                                     MetaWindow   *window);
static gboolean ooze_global_menu_window_is_inkscape (MetaWindow *window);
static MetaWindow *ooze_global_menu_find_window_by_xid (OozeGlobalMenu *menu,
                                                       guint32         xid);
static void ooze_global_menu_log_registrar_miss (OozeGlobalMenu *menu,
                                                 MetaWindow     *window);
static void ooze_global_menu_ensure_registrar_watch (OozeGlobalMenu *menu);
static void ooze_global_menu_ensure_registrar_service (OozeGlobalMenu *menu);
static void ooze_global_menu_cancel_bind_query (OozeGlobalMenu *menu);
static void on_dbusmenu_changed (gpointer user_data);
static void ooze_global_menu_bind_window_with_registrar (OozeGlobalMenu *menu,
                                                         MetaWindow     *window,
                                                         const char     *registrar_bus,
                                                         const char     *registrar_menubar);
static gboolean ooze_global_menu_action_sensitive (OozeGlobalMenu *menu,
                                                 const char   *action);
static void ooze_global_menu_append_model (OozeGlobalMenu *menu,
                                         GMenuModel   *model,
                                         GPtrArray    *rows,
                                         const char   *prefix);

static void
ooze_global_menu_emit_changed (OozeGlobalMenu *menu)
{
  if (menu->suppress_changed)
    return;

  if (menu->changed_cb)
    menu->changed_cb (menu->changed_data);
}

static void
ooze_global_menu_clear_entries (OozeGlobalMenu *menu)
{
  gsize i;

  if (menu->actions)
    {
      for (i = 0; i < menu->n_actions; i++)
        {
          g_free (menu->actions[i].action);
          g_clear_pointer (&menu->actions[i].target, g_variant_unref);
        }
      g_clear_pointer (&menu->actions, g_free);
      menu->n_actions = 0;
    }

  if (menu->entries)
    {
      for (i = 0; i < menu->n_entries; i++)
        g_free ((char *) menu->entries[i].label);
      g_clear_pointer (&menu->entries, g_free);
      menu->n_entries = 0;
    }
}

static void
ooze_global_menu_clear_submenu_cache (OozeGlobalMenu *menu)
{
  guint i;

  if (!menu)
    return;

  for (i = 0; i < OOZE_GLOBAL_MENU_MAX_TOP; i++)
    {
      if (menu->submenu_models[i] && menu->submenu_handlers[i])
        g_clear_signal_handler (&menu->submenu_handlers[i],
                                menu->submenu_models[i]);
      menu->submenu_handlers[i] = 0;
      g_clear_object (&menu->submenu_models[i]);
    }
}

static void
on_cached_submenu_items_changed (GMenuModel   *model G_GNUC_UNUSED,
                                 gint          position G_GNUC_UNUSED,
                                 gint          removed G_GNUC_UNUSED,
                                 gint          added G_GNUC_UNUSED,
                                 OozeGlobalMenu *menu)
{
  /* Submenu Start() finished — reopen pending dropdown / refresh tops. */
  if (menu->pending_top_valid)
    ooze_global_menu_emit_changed (menu);
}

static void
ooze_global_menu_prime_submenus (OozeGlobalMenu *menu)
{
  guint n;
  guint i;

  if (!menu || !menu->menubar)
    return;

  ooze_global_menu_clear_submenu_cache (menu);

  menu->suppress_changed++;
  n = ooze_global_menu_get_n_top (menu);
  for (i = 0; i < n && i < OOZE_GLOBAL_MENU_MAX_TOP; i++)
    {
      GMenuModel *link;

      link = g_menu_model_get_item_link (menu->menubar, (int) i,
                                         G_MENU_LINK_SUBMENU);
      if (!link)
        link = g_menu_model_get_item_link (menu->menubar, (int) i,
                                           G_MENU_LINK_SECTION);
      if (!link)
        continue;

      /* Keep the ref — dropping it cancels GDBusMenuModel's async Start(). */
      menu->submenu_models[i] = link;
      (void) g_menu_model_get_n_items (link);
      menu->submenu_handlers[i] =
        g_signal_connect (link, "items-changed",
                          G_CALLBACK (on_cached_submenu_items_changed), menu);
    }
  menu->suppress_changed--;
}

static void
on_menubar_items_changed (GMenuModel   *model G_GNUC_UNUSED,
                          gint          position G_GNUC_UNUSED,
                          gint          removed G_GNUC_UNUSED,
                          gint          added G_GNUC_UNUSED,
                          OozeGlobalMenu *menu)
{
  ooze_global_menu_schedule_prime (menu);
  ooze_global_menu_emit_changed (menu);
}

static gboolean
ooze_global_menu_prime_idle (gpointer user_data)
{
  OozeGlobalMenu *menu = user_data;

  menu->prime_idle = 0;
  ooze_global_menu_prime_submenus (menu);
  return G_SOURCE_REMOVE;
}

static void
ooze_global_menu_schedule_prime (OozeGlobalMenu *menu)
{
  if (!menu || menu->prime_idle != 0)
    return;

  menu->prime_idle = g_idle_add (ooze_global_menu_prime_idle, menu);
}

static void
ooze_global_menu_cancel_focus_sync (OozeGlobalMenu *menu)
{
  if (!menu || menu->focus_sync_idle == 0)
    return;

  g_source_remove (menu->focus_sync_idle);
  menu->focus_sync_idle = 0;
}

static gboolean
ooze_global_menu_focus_sync_idle (gpointer user_data)
{
  OozeGlobalMenu *menu = user_data;

  menu->focus_sync_idle = 0;
  ooze_global_menu_sync_focus (menu);
  return G_SOURCE_REMOVE;
}

static void
ooze_global_menu_schedule_focus_sync (OozeGlobalMenu *menu)
{
  if (!menu)
    return;

  ooze_global_menu_cancel_focus_sync (menu);
  menu->focus_sync_idle =
    g_timeout_add (OOZE_FOCUS_SYNC_DEBOUNCE_MS,
                   ooze_global_menu_focus_sync_idle,
                   menu);
}

static void
ooze_global_menu_flush_focus_sync (OozeGlobalMenu *menu)
{
  if (!menu || menu->focus_sync_idle == 0)
    return;

  ooze_global_menu_cancel_focus_sync (menu);
  ooze_global_menu_sync_focus (menu);
}

static void
ooze_global_menu_clear_pending_submenu (OozeGlobalMenu *menu)
{
  if (menu->pending_submenu)
    g_clear_signal_handler (&menu->pending_submenu_handler, menu->pending_submenu);

  g_clear_object (&menu->pending_submenu);
  menu->pending_top_valid = FALSE;
}

static void
on_pending_submenu_items_changed (GMenuModel   *model G_GNUC_UNUSED,
                                  gint          position G_GNUC_UNUSED,
                                  gint          removed G_GNUC_UNUSED,
                                  gint          added G_GNUC_UNUSED,
                                  OozeGlobalMenu *menu)
{
  if (menu->pending_submenu)
    g_clear_signal_handler (&menu->pending_submenu_handler, menu->pending_submenu);

  g_clear_object (&menu->pending_submenu);
  /* Keep pending_top_valid so the panel can reopen this top item. */
  ooze_global_menu_emit_changed (menu);
}

static void
ooze_global_menu_clear_pending_activate (OozeGlobalMenu *menu)
{
  if (menu->activate_idle)
    {
      g_source_remove (menu->activate_idle);
      menu->activate_idle = 0;
    }
  g_clear_pointer (&menu->pending_action, g_free);
  g_clear_pointer (&menu->pending_target, g_variant_unref);
}

static void
ooze_global_menu_kick_actions (GActionGroup *group)
{
  g_auto (GStrv) names = NULL;

  if (!group)
    return;

  /* Kick the GDBusActionGroup round-trip so ListActions populates. */
  names = g_action_group_list_actions (group);
}

typedef struct
{
  OozeGlobalMenu *menu;
  guint32       xids[2];
  guint         n_xids;
  guint         index;
} OozeGlobalMenuRegistrarQuery;

static void
ooze_global_menu_registrar_query_step (OozeGlobalMenuRegistrarQuery *query);

static void
ooze_global_menu_registrar_query_done (OozeGlobalMenuRegistrarQuery *query,
                                       const char                   *bus,
                                       const char                   *menubar)
{
  OozeGlobalMenu *menu = query->menu;
  MetaWindow *window = menu->pending_bind_window;

  g_free (query);

  if (window)
    g_object_remove_weak_pointer (G_OBJECT (window),
                                  (gpointer *) &menu->pending_bind_window);
  menu->pending_bind_window = NULL;
  g_clear_object (&menu->bind_cancellable);

  /* Focus moved on while the query was in flight. */
  if (!window || window != menu->watched_window)
    return;

  ooze_global_menu_bind_window_with_registrar (menu, window, bus, menubar);
}

static void
ooze_global_menu_registrar_query_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  OozeGlobalMenuRegistrarQuery *query = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_free (query);
      return;
    }

  if (reply)
    {
      g_autofree char *bus = NULL;
      g_autofree char *menubar = NULL;

      g_variant_get (reply, "(so)", &bus, &menubar);
      if (bus && *bus && menubar && *menubar)
        {
          ooze_global_menu_registrar_query_done (query, bus, menubar);
          return;
        }
    }

  query->index++;
  if (query->index < query->n_xids)
    {
      ooze_global_menu_registrar_query_step (query);
      return;
    }

  ooze_global_menu_registrar_query_done (query, NULL, NULL);
}

static void
ooze_global_menu_registrar_query_step (OozeGlobalMenuRegistrarQuery *query)
{
  OozeGlobalMenu *menu = query->menu;

  g_dbus_connection_call (menu->session,
                          "com.canonical.AppMenu.Registrar",
                          "/com/canonical/AppMenu/Registrar",
                          "com.canonical.AppMenu.Registrar",
                          "GetMenuForWindow",
                          g_variant_new ("(u)", query->xids[query->index]),
                          G_VARIANT_TYPE ("(so)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          800,
                          menu->bind_cancellable,
                          ooze_global_menu_registrar_query_cb,
                          query);
}

static void
ooze_global_menu_cancel_bind_query (OozeGlobalMenu *menu)
{
  if (menu->bind_cancellable)
    {
      g_cancellable_cancel (menu->bind_cancellable);
      g_clear_object (&menu->bind_cancellable);
    }
  if (menu->pending_bind_window)
    {
      g_object_remove_weak_pointer (G_OBJECT (menu->pending_bind_window),
                                    (gpointer *) &menu->pending_bind_window);
      menu->pending_bind_window = NULL;
    }
}

/* GTK appmenu-gtk-module registers the Gdk toplevel XID; Mutter exposes
 * both the client xwindow and the toplevel — try both, asynchronously. */
static void
ooze_global_menu_query_appmenu_registrar_async (OozeGlobalMenu *menu,
                                                MetaWindow     *window)
{
  OozeGlobalMenuRegistrarQuery *query;
  guint32 xid;
  guint32 toplevel_xid;

  ooze_global_menu_cancel_bind_query (menu);

  if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_X11)
    {
      ooze_global_menu_bind_window_with_registrar (menu, window, NULL, NULL);
      return;
    }

  ooze_global_menu_ensure_registrar_service (menu);

  xid = (guint32) meta_window_x11_get_xwindow (window);
  toplevel_xid = (guint32) meta_window_x11_get_toplevel_xwindow (window);

  query = g_new0 (OozeGlobalMenuRegistrarQuery, 1);
  query->menu = menu;
  if (xid != 0)
    query->xids[query->n_xids++] = xid;
  if (toplevel_xid != 0 && toplevel_xid != xid)
    query->xids[query->n_xids++] = toplevel_xid;

  if (query->n_xids == 0)
    {
      g_free (query);
      ooze_global_menu_bind_window_with_registrar (menu, window, NULL, NULL);
      return;
    }

  menu->bind_cancellable = g_cancellable_new ();
  menu->pending_bind_window = window;
  g_object_add_weak_pointer (G_OBJECT (window),
                             (gpointer *) &menu->pending_bind_window);
  ooze_global_menu_registrar_query_step (query);
}

static void
ooze_global_menu_cancel_registrar_retry (OozeGlobalMenu *menu)
{
  if (!menu)
    return;

  if (menu->registrar_retry_id)
    {
      g_source_remove (menu->registrar_retry_id);
      menu->registrar_retry_id = 0;
    }
  menu->registrar_retry_left = 0;
  menu->registrar_retry_window = NULL;
}

static gboolean
ooze_global_menu_registrar_retry_cb (gpointer user_data)
{
  OozeGlobalMenu *menu = user_data;
  MetaWindow *window = menu->registrar_retry_window;

  menu->registrar_retry_id = 0;

  if (!window || window != menu->watched_window)
    {
      menu->registrar_retry_left = 0;
      menu->registrar_retry_window = NULL;
      return G_SOURCE_REMOVE;
    }

  if (ooze_global_menu_has_dbusmenu (menu) ||
      (menu->menubar && g_menu_model_get_n_items (menu->menubar) > 0))
    {
      menu->registrar_retry_left = 0;
      menu->registrar_retry_window = NULL;
      return G_SOURCE_REMOVE;
    }

  if (menu->registrar_retry_left == 0)
    {
      ooze_global_menu_log_registrar_miss (menu, window);
      menu->registrar_retry_window = NULL;
      return G_SOURCE_REMOVE;
    }

  menu->registrar_retry_left--;
  ooze_global_menu_bind_window (menu, window);

  /* bind_window may have scheduled the next attempt or cancelled on success. */
  if (menu->registrar_retry_id != 0)
    return G_SOURCE_REMOVE;

  if (ooze_global_menu_has_dbusmenu (menu) ||
      (menu->menubar && g_menu_model_get_n_items (menu->menubar) > 0))
    {
      ooze_global_menu_cancel_registrar_retry (menu);
      return G_SOURCE_REMOVE;
    }

  if (menu->registrar_retry_left == 0)
    {
      ooze_global_menu_log_registrar_miss (menu, window);
      menu->registrar_retry_window = NULL;
      return G_SOURCE_REMOVE;
    }

  menu->registrar_retry_id =
    g_timeout_add (250, ooze_global_menu_registrar_retry_cb, menu);
  return G_SOURCE_REMOVE;
}

static void
ooze_global_menu_schedule_registrar_retry (OozeGlobalMenu *menu,
                                         MetaWindow   *window)
{
  if (!ooze_appmenu_foreign_enabled ())
    return;
  if (!menu || !window)
    return;
  if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_X11)
    return;
  if (ooze_global_menu_has_dbusmenu (menu))
    return;

  menu->registrar_retry_window = window;
  if (menu->registrar_retry_id != 0)
    return;

  if (menu->registrar_retry_left == 0)
    {
      menu->registrar_retry_left =
        ooze_global_menu_window_is_inkscape (window)
          ? OOZE_REGISTRAR_RETRY_INKSCAPE
          : OOZE_REGISTRAR_RETRY_DEFAULT;
    }

  menu->registrar_retry_id =
    g_timeout_add (250, ooze_global_menu_registrar_retry_cb, menu);
}

static void
on_appmenu_window_registered (GDBusConnection *connection G_GNUC_UNUSED,
                              const char      *sender G_GNUC_UNUSED,
                              const char      *path G_GNUC_UNUSED,
                              const char      *iface G_GNUC_UNUSED,
                              const char      *signal G_GNUC_UNUSED,
                              GVariant        *params,
                              gpointer         user_data)
{
  OozeGlobalMenu *menu = user_data;
  guint32 xid = 0;
  MetaWindow *window;

  if (!menu || !params)
    return;

  g_variant_get (params, "(u&s&o)", &xid, NULL, NULL);
  if (xid == 0)
    return;

  window = ooze_global_menu_find_window_by_xid (menu, xid);
  if (!window)
    {
      g_print ("Ooze global menu: WindowRegistered xid=%u — no MetaWindow yet\n",
               xid);
      return;
    }

  /* Inkscape may have started before ShellShowsMenubar was visible. */
  ooze_appmenu_ensure_shell_shows_menubar ();
  g_print ("Ooze global menu: WindowRegistered xid=%u → \"%s\"\n",
           xid,
           meta_window_get_title (window) ? meta_window_get_title (window) : "?");
  ooze_global_menu_bind_window (menu, window);
}

static void
ooze_global_menu_registrar_start_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (!reply &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Ooze global menu: could not activate AppMenu registrar: %s "
               "(install appmenu-registrar / run scripts/install-appmenu.sh)",
               error ? error->message : "unknown");
}

static void
ooze_global_menu_registrar_owner_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  OozeGlobalMenu *menu = user_data;
  g_autoptr (GVariant) owner = NULL;

  owner = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, NULL);
  if (owner)
    return;

  g_dbus_connection_call (menu->session,
                          "org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "StartServiceByName",
                          g_variant_new ("(su)",
                                         "com.canonical.AppMenu.Registrar",
                                         0u),
                          G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          3000,
                          menu->cancellable,
                          ooze_global_menu_registrar_start_cb,
                          NULL);
}

/* Async counterpart of ooze_appmenu_ensure_registrar() — never blocks the
 * compositor thread. */
static void
ooze_global_menu_ensure_registrar_service (OozeGlobalMenu *menu)
{
  if (!ooze_appmenu_foreign_enabled ())
    return;
  if (!menu->session || menu->registrar_service_requested)
    return;

  menu->registrar_service_requested = TRUE;
  g_dbus_connection_call (menu->session,
                          "org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "GetNameOwner",
                          g_variant_new ("(s)",
                                         "com.canonical.AppMenu.Registrar"),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          500,
                          menu->cancellable,
                          ooze_global_menu_registrar_owner_cb,
                          menu);
}

static void
ooze_global_menu_ensure_registrar_watch (OozeGlobalMenu *menu)
{
  if (!menu || menu->registrar_signal_id != 0)
    return;

  if (!menu->session)
    return;

  ooze_global_menu_ensure_registrar_service (menu);

  menu->registrar_signal_id =
    g_dbus_connection_signal_subscribe (menu->session,
                                        NULL,
                                        "com.canonical.AppMenu.Registrar",
                                        "WindowRegistered",
                                        "/com/canonical/AppMenu/Registrar",
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        on_appmenu_window_registered,
                                        menu,
                                        NULL);
}

static void
ooze_global_menu_clear_proxy (OozeGlobalMenu *menu)
{
  ooze_global_menu_clear_entries (menu);
  ooze_global_menu_clear_pending_submenu (menu);
  ooze_global_menu_clear_pending_activate (menu);
  ooze_global_menu_clear_submenu_cache (menu);

  if (menu->prime_idle)
    {
      g_source_remove (menu->prime_idle);
      menu->prime_idle = 0;
    }

  if (menu->menubar && menu->items_handler)
    g_clear_signal_handler (&menu->items_handler, menu->menubar);

  g_clear_object (&menu->menubar);
  g_clear_object (&menu->app_actions);
  g_clear_object (&menu->win_actions);
  if (menu->dbusmenu)
    {
      ooze_dbusmenu_free (menu->dbusmenu);
      menu->dbusmenu = NULL;
    }
  ooze_dbusmenu_items_free (menu->dbus_tops, menu->n_dbus_tops);
  menu->dbus_tops = NULL;
  menu->n_dbus_tops = 0;
  g_clear_pointer (&menu->bus_name, g_free);
  g_clear_pointer (&menu->menubar_path, g_free);
  g_clear_pointer (&menu->app_path, g_free);
  g_clear_pointer (&menu->win_path, g_free);
  menu->bound_window = NULL;
}

static gboolean
ooze_global_menu_paths_equal (OozeGlobalMenu *menu,
                            const char   *bus,
                            const char   *menubar,
                            const char   *app,
                            const char   *win)
{
  return g_strcmp0 (menu->bus_name, bus) == 0 &&
         g_strcmp0 (menu->menubar_path, menubar) == 0 &&
         g_strcmp0 (menu->app_path, app) == 0 &&
         g_strcmp0 (menu->win_path, win) == 0;
}

static void
on_watched_gtk_props_changed (MetaWindow  *window,
                              GParamSpec  *pspec G_GNUC_UNUSED,
                              OozeGlobalMenu *menu)
{
  if (!menu || window != menu->watched_window)
    return;

  ooze_global_menu_bind_window (menu, window);
}

static void
on_watched_window_gone (gpointer  data,
                        GObject *where_the_object_was)
{
  OozeGlobalMenu *menu = data;

  menu->watched_window = NULL;
  menu->watch_bus_handler = 0;
  menu->watch_menubar_handler = 0;
  menu->watch_appmenu_handler = 0;
  menu->watch_app_handler = 0;
  menu->watch_win_handler = 0;
  if (menu->registrar_retry_window == (MetaWindow *) where_the_object_was)
    ooze_global_menu_cancel_registrar_retry (menu);
  if (menu->bound_window == (MetaWindow *) where_the_object_was)
    {
      ooze_global_menu_clear_proxy (menu);
      ooze_global_menu_emit_changed (menu);
    }
}

static void
ooze_global_menu_unwatch_window (OozeGlobalMenu *menu)
{
  MetaWindow *window;

  if (!menu)
    return;

  window = menu->watched_window;
  if (window)
    {
      g_object_weak_unref (G_OBJECT (window), on_watched_window_gone, menu);
      g_clear_signal_handler (&menu->watch_bus_handler, window);
      g_clear_signal_handler (&menu->watch_menubar_handler, window);
      g_clear_signal_handler (&menu->watch_appmenu_handler, window);
      g_clear_signal_handler (&menu->watch_app_handler, window);
      g_clear_signal_handler (&menu->watch_win_handler, window);
    }
  else
    {
      /* Window already finalized — drop stale ids. */
      menu->watch_bus_handler = 0;
      menu->watch_menubar_handler = 0;
      menu->watch_appmenu_handler = 0;
      menu->watch_app_handler = 0;
      menu->watch_win_handler = 0;
    }

  menu->watched_window = NULL;
}

static void
ooze_global_menu_watch_window (OozeGlobalMenu *menu,
                             MetaWindow   *window)
{
  if (menu->watched_window == window)
    return;

  ooze_global_menu_unwatch_window (menu);
  if (!window)
    return;

  menu->watched_window = window;
  g_object_weak_ref (G_OBJECT (window), on_watched_window_gone, menu);

  menu->watch_bus_handler =
    g_signal_connect (window, "notify::gtk-unique-bus-name",
                      G_CALLBACK (on_watched_gtk_props_changed), menu);
  menu->watch_menubar_handler =
    g_signal_connect (window, "notify::gtk-menubar-object-path",
                      G_CALLBACK (on_watched_gtk_props_changed), menu);
  menu->watch_appmenu_handler =
    g_signal_connect (window, "notify::gtk-app-menu-object-path",
                      G_CALLBACK (on_watched_gtk_props_changed), menu);
  menu->watch_app_handler =
    g_signal_connect (window, "notify::gtk-application-object-path",
                      G_CALLBACK (on_watched_gtk_props_changed), menu);
  menu->watch_win_handler =
    g_signal_connect (window, "notify::gtk-window-object-path",
                      G_CALLBACK (on_watched_gtk_props_changed), menu);
}

static gboolean
ooze_global_menu_window_matches_xid (MetaWindow *window,
                                     guint32     xid)
{
  if (!window || xid == 0)
    return FALSE;
  if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_X11)
    return FALSE;

  return (guint32) meta_window_x11_get_xwindow (window) == xid ||
         (guint32) meta_window_x11_get_toplevel_xwindow (window) == xid;
}

static gboolean
ooze_global_menu_window_is_inkscape (MetaWindow *window)
{
  const char *wm_class;

  if (!window)
    return FALSE;

  wm_class = meta_window_get_wm_class (window);
  return wm_class && g_ascii_strcasecmp (wm_class, "Inkscape") == 0;
}

static MetaWindow *
ooze_global_menu_find_window_by_xid (OozeGlobalMenu *menu,
                                     guint32         xid)
{
  MetaWindow *focus;
  MetaWindow *match = NULL;
  GList *windows;
  GList *l;

  if (!menu || !menu->display || xid == 0)
    return NULL;

  focus = meta_display_get_focus_window (menu->display);
  if (ooze_global_menu_window_matches_xid (focus, xid))
    return focus;

  if (ooze_global_menu_window_matches_xid (menu->watched_window, xid))
    return menu->watched_window;

  windows = meta_display_list_all_windows (menu->display);
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (ooze_global_menu_window_matches_xid (window, xid))
        {
          match = window;
          break;
        }
    }
  g_list_free (windows);
  return match;
}

static void
ooze_global_menu_log_registrar_miss (OozeGlobalMenu *menu,
                                     MetaWindow     *window)
{
  guint32 xid = 0;
  guint32 toplevel = 0;

  if (!menu || !window)
    return;
  if (!ooze_global_menu_window_is_inkscape (window))
    return;
  if (meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_X11)
    return;

  xid = (guint32) meta_window_x11_get_xwindow (window);
  toplevel = (guint32) meta_window_x11_get_toplevel_xwindow (window);

  g_warning ("Ooze global menu: no menu export for X11 \"%s\" wm_class=%s "
             "xid=%u toplevel=%u "
             "gtk_bus=%s gtk_menubar=%s — "
             "in-app bar may remain if ShellShowsMenubar was late",
             meta_window_get_title (window) ? meta_window_get_title (window) : "?",
             meta_window_get_wm_class (window)
               ? meta_window_get_wm_class (window) : "?",
             xid, toplevel,
             meta_window_get_gtk_unique_bus_name (window)
               ? meta_window_get_gtk_unique_bus_name (window) : "-",
             meta_window_get_gtk_menubar_object_path (window)
               ? meta_window_get_gtk_menubar_object_path (window) : "-");
}

static gboolean
ooze_global_menu_is_wayland_classic_menubar_app (MetaWindow *window)
{
  if (!window ||
      meta_window_get_client_type (window) != META_WINDOW_CLIENT_TYPE_WAYLAND)
    return FALSE;

  return ooze_global_menu_window_is_inkscape (window);
}

static void
ooze_global_menu_set_x11_launch_hint (OozeGlobalMenu *menu,
                                      const char     *hint)
{
  if (!menu)
    return;

  if (g_strcmp0 (menu->x11_launch_hint, hint) == 0)
    return;

  g_free (menu->x11_launch_hint);
  menu->x11_launch_hint = hint ? g_strdup (hint) : NULL;
}

static void
ooze_global_menu_handle_wayland_classic (OozeGlobalMenu *menu,
                                         MetaWindow     *window)
{
  static gboolean warned_wayland_gtk3;

  ooze_global_menu_cancel_registrar_retry (menu);
  ooze_global_menu_clear_proxy (menu);
  ooze_global_menu_set_x11_launch_hint (menu, NULL);

  if (ooze_appmenu_foreign_enabled ())
    {
      ooze_global_menu_set_x11_launch_hint (menu, "Needs X11 (Spot/Command)");
      if (!warned_wayland_gtk3)
        {
          g_warning ("Ooze global menu: %s is Wayland-native; "
                     "foreign global menus need Xwayland — relaunch via "
                     "Spot/Command so GDK_BACKEND=x11 is set "
                     "(or unset OOZE_FOREIGN_GLOBAL_MENU for in-window menus)",
                     meta_window_get_wm_class (window)
                       ? meta_window_get_wm_class (window) : "app");
          warned_wayland_gtk3 = TRUE;
        }
      g_print ("Ooze global menu: Wayland classic GtkMenuBar focused "
               "(wm_class=%s) — no registrar export; use Spot/Command\n",
               meta_window_get_wm_class (window)
                 ? meta_window_get_wm_class (window) : "?");
    }

  ooze_global_menu_emit_changed (menu);
  (void) window;
}

static void
ooze_global_menu_bind_window (OozeGlobalMenu *menu,
                            MetaWindow   *window)
{
  g_autoptr (OozeStallScope) stall = NULL;
  gboolean is_x11;

  stall = ooze_stall_begin ("focus-bind");

  ooze_global_menu_cancel_bind_query (menu);
  ooze_global_menu_watch_window (menu, window);
  ooze_global_menu_ensure_registrar_watch (menu);

  if (!window)
    {
      ooze_global_menu_cancel_registrar_retry (menu);
      ooze_global_menu_set_x11_launch_hint (menu, NULL);
      if (menu->menubar || menu->dbusmenu)
        {
          ooze_global_menu_clear_proxy (menu);
          ooze_global_menu_emit_changed (menu);
        }
      return;
    }

  is_x11 = meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_X11;

  /* Wayland org_kde_kwin_appmenu announcement (Qt/appmenu-gtk-module). */
  if (!is_x11 && menu->wl_appmenu && menu->session)
    {
      const char *service = NULL;
      const char *path = NULL;

      if (ooze_appmenu_wayland_lookup (menu->wl_appmenu, window,
                                       &service, &path))
        {
          ooze_global_menu_cancel_registrar_retry (menu);
          ooze_global_menu_set_x11_launch_hint (menu, NULL);
          ooze_global_menu_bind_window_with_registrar (menu, window,
                                                       service, path);
          return;
        }
    }

  /* Native Wayland classic GtkMenuBar (Inkscape): cannot use appmenu registrar. */
  if (ooze_global_menu_is_wayland_classic_menubar_app (window))
    {
      ooze_global_menu_handle_wayland_classic (menu, window);
      return;
    }

  ooze_global_menu_set_x11_launch_hint (menu, NULL);

  /*
   * Foreign X11 AppMenu (registrar / dbusmenu) is off by default.
   * Wayland gtk-shell exports (Ooze apps) still bind below.
   */
  if (is_x11 && !ooze_appmenu_foreign_enabled ())
    {
      ooze_global_menu_cancel_registrar_retry (menu);
      if (menu->menubar || menu->dbusmenu)
        {
          ooze_global_menu_clear_proxy (menu);
          ooze_global_menu_emit_changed (menu);
        }
      else
        menu->bound_window = NULL;
      return;
    }

  /*
   * X11 / Xwayland + appmenu-gtk-module (OOZE_FOREIGN_GLOBAL_MENU=1 only):
   * - Legacy exporters call AppMenu.Registrar (com.canonical.dbusmenu).
   * - Modern appmenu-gtk-module (Ubuntu 25.04+) exports org.gtk.Menus and sets
   *   _GTK_UNIQUE_BUS_NAME / _GTK_MENUBAR_OBJECT_PATH — same gtk-shell props
   *   Mutter exposes on MetaWindow. Prefer registrar when present; otherwise
   *   bind those props (not empty unlabeled stubs from bare Wayland export).
   * The registrar lookup is asynchronous; binding completes in
   * ooze_global_menu_bind_window_with_registrar().
   */
  if (ooze_appmenu_foreign_enabled () && menu->session && is_x11)
    {
      ooze_appmenu_ensure_shell_shows_menubar ();
      ooze_global_menu_query_appmenu_registrar_async (menu, window);
      return;
    }

  ooze_global_menu_bind_window_with_registrar (menu, window, NULL, NULL);
}

/* Legacy dbusmenu layout data landed — refresh the cached tops. */
static void
on_dbusmenu_changed (gpointer user_data)
{
  OozeGlobalMenu *menu = user_data;
  OozeDbusmenuItem *tops = NULL;
  gsize n_tops = 0;

  if (!menu->dbusmenu)
    return;

  if (ooze_dbusmenu_get_top_items (menu->dbusmenu, &tops, &n_tops))
    {
      ooze_dbusmenu_items_free (menu->dbus_tops, menu->n_dbus_tops);
      menu->dbus_tops = tops;
      menu->n_dbus_tops = n_tops;
    }

  ooze_global_menu_emit_changed (menu);
}

static void
ooze_global_menu_bind_window_with_registrar (OozeGlobalMenu *menu,
                                             MetaWindow     *window,
                                             const char     *registrar_bus,
                                             const char     *registrar_menubar)
{
  const char *bus = NULL;
  const char *menubar = NULL;
  const char *app_path = NULL;
  const char *win_path = NULL;
  gboolean have_export;
  gboolean is_x11;

  is_x11 = meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_X11;

  if (registrar_bus && *registrar_bus && registrar_menubar && *registrar_menubar)
    {
      bus = registrar_bus;
      menubar = registrar_menubar;
    }
  else
    {
      registrar_bus = NULL;
      registrar_menubar = NULL;
      bus = meta_window_get_gtk_unique_bus_name (window);
      menubar = meta_window_get_gtk_menubar_object_path (window);
      /* Inkscape / older GTK often export the app-menu path instead of menubar. */
      if (!menubar || !*menubar)
        menubar = meta_window_get_gtk_app_menu_object_path (window);
      app_path = meta_window_get_gtk_application_object_path (window);
      win_path = meta_window_get_gtk_window_object_path (window);
    }

  have_export = bus && *bus && menubar && *menubar;

  /*
   * GTK shell props often arrive after focus. Keep the existing menu only
   * while this is still the same bound window. Focus moved to a different
   * client with no export yet — drop the old app menu immediately so Spot
   * never sticks under Inkscape / Command.
   */
  if (!have_export)
    {
      if (menu->bound_window == window && (menu->menubar || menu->dbusmenu))
        {
          if (ooze_appmenu_foreign_enabled ())
            ooze_global_menu_schedule_registrar_retry (menu, window);
          return;
        }

      if (menu->menubar || menu->dbusmenu)
        {
          ooze_global_menu_clear_proxy (menu);
          ooze_global_menu_emit_changed (menu);
        }
      else
        menu->bound_window = NULL;

      if (ooze_appmenu_foreign_enabled ())
        ooze_global_menu_schedule_registrar_retry (menu, window);
      return;
    }

  ooze_global_menu_cancel_registrar_retry (menu);

  if ((menu->menubar || menu->dbusmenu) &&
      ooze_global_menu_paths_equal (menu, bus, menubar, app_path, win_path))
    {
      menu->bound_window = window;
      if (!menu->dbusmenu)
        {
          if (!menu->win_actions && win_path && menu->session && menu->bus_name)
            {
              g_free (menu->win_path);
              menu->win_path = g_strdup (win_path);
              menu->win_actions =
                G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                        menu->bus_name,
                                                        menu->win_path));
              ooze_global_menu_kick_actions (menu->win_actions);
            }
          if (!menu->app_actions && app_path && menu->session && menu->bus_name)
            {
              g_free (menu->app_path);
              menu->app_path = g_strdup (app_path);
              menu->app_actions =
                G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                        menu->bus_name,
                                                        menu->app_path));
              ooze_global_menu_kick_actions (menu->app_actions);
            }
        }
      return;
    }

  /*
   * Same application, different window: keep the shared menubar/app actions
   * and only retarget win.* actions. Full rebinds caused empty-submenu
   * misses (extra clicks) after minimize / multi-window focus changes.
   */
  if (menu->menubar &&
      g_strcmp0 (menu->bus_name, bus) == 0 &&
      g_strcmp0 (menu->menubar_path, menubar) == 0 &&
      g_strcmp0 (menu->app_path, app_path) == 0)
    {
      menu->bound_window = window;
      if (g_strcmp0 (menu->win_path, win_path) != 0)
        {
          g_clear_object (&menu->win_actions);
          g_free (menu->win_path);
          menu->win_path = g_strdup (win_path);
          if (menu->win_path)
            {
              menu->win_actions =
                G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                        menu->bus_name,
                                                        menu->win_path));
              ooze_global_menu_kick_actions (menu->win_actions);
            }
        }
      return;
    }

  ooze_global_menu_clear_proxy (menu);

  /* Session bus is acquired asynchronously at startup; a re-bind runs
   * once it is ready. */
  if (!menu->session)
    return;

  menu->bound_window = window;
  menu->bus_name = g_strdup (bus);
  menu->menubar_path = g_strdup (menubar);
  menu->app_path = g_strdup (app_path);
  menu->win_path = g_strdup (win_path);

  /* Registrar paths are dbusmenu; gtk-shell / modern appmenu paths are org.gtk.Menus. */
  if (registrar_bus && registrar_menubar)
    {
      /* Don't wrap dbusmenu paths as GDBusMenuModel — that yields "Menu" stubs. */
      OozeDbusmenu *dm = ooze_dbusmenu_new (menu->session,
                                        menu->bus_name,
                                        menu->menubar_path);

      if (!dm)
        {
          g_clear_pointer (&menu->bus_name, g_free);
          g_clear_pointer (&menu->menubar_path, g_free);
          g_clear_pointer (&menu->app_path, g_free);
          g_clear_pointer (&menu->win_path, g_free);
          menu->bound_window = NULL;
          ooze_global_menu_schedule_registrar_retry (menu, window);
          return;
        }

      g_print ("Ooze global menu: dbusmenu bound (%s %s) title=\"%s\"\n",
               menu->bus_name, menu->menubar_path,
               meta_window_get_title (window) ?
                 meta_window_get_title (window) : "");

      /* Tops arrive asynchronously; on_dbusmenu_changed refreshes them. */
      menu->dbusmenu = dm;
      ooze_dbusmenu_set_changed_callback (dm, on_dbusmenu_changed, menu);
      ooze_dbusmenu_start (dm);
      return;
    }

  menu->menubar = G_MENU_MODEL (g_dbus_menu_model_get (menu->session,
                                                       menu->bus_name,
                                                       menu->menubar_path));
  if (menu->app_path)
    {
      menu->app_actions = G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                                   menu->bus_name,
                                                                   menu->app_path));
      ooze_global_menu_kick_actions (menu->app_actions);
    }
  if (menu->win_path)
    {
      menu->win_actions = G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                                   menu->bus_name,
                                                                   menu->win_path));
      ooze_global_menu_kick_actions (menu->win_actions);
    }

  menu->items_handler =
    g_signal_connect (menu->menubar, "items-changed",
                      G_CALLBACK (on_menubar_items_changed), menu);

  g_print ("Ooze global menu: gtk-shell bound (%s %s) title=\"%s\" app_id=%s%s\n",
           menu->bus_name,
           menu->menubar_path,
           meta_window_get_title (window) ? meta_window_get_title (window) : "",
           meta_window_get_gtk_application_id (window) ?
             meta_window_get_gtk_application_id (window) : "?",
           is_x11 ? " (X11 appmenu GMenu)" : "");

  ooze_global_menu_schedule_prime (menu);
  ooze_global_menu_emit_changed (menu);
}

static void
on_focus_window_changed (MetaDisplay  *display G_GNUC_UNUSED,
                         GParamSpec   *pspec G_GNUC_UNUSED,
                         OozeGlobalMenu *menu)
{
  ooze_global_menu_schedule_focus_sync (menu);
}

static void
on_session_bus_ready (GObject      *source G_GNUC_UNUSED,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  OozeGlobalMenu *menu;
  g_autoptr (GError) error = NULL;
  GDBusConnection *session;

  session = g_bus_get_finish (res, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_clear_object (&session);
      return;
    }

  menu = user_data;
  if (!session)
    {
      g_warning ("Ooze global menu: session bus unavailable: %s",
                 error ? error->message : "unknown");
      return;
    }

  menu->session = session;
  ooze_global_menu_ensure_registrar_watch (menu);
  ooze_global_menu_sync_focus (menu);
}

OozeGlobalMenu *
ooze_global_menu_new (MetaDisplay *display)
{
  OozeGlobalMenu *menu;

  g_return_val_if_fail (META_IS_DISPLAY (display), NULL);

  menu = g_new0 (OozeGlobalMenu, 1);
  menu->display = display;
  menu->cancellable = g_cancellable_new ();
  menu->focus_handler =
    g_signal_connect (display, "notify::focus-window",
                      G_CALLBACK (on_focus_window_changed), menu);

  g_bus_get (G_BUS_TYPE_SESSION, menu->cancellable,
             on_session_bus_ready, menu);
  ooze_global_menu_sync_focus (menu);
  return menu;
}

void
ooze_global_menu_free (OozeGlobalMenu *menu)
{
  if (!menu)
    return;

  if (menu->display && menu->focus_handler)
    g_signal_handler_disconnect (menu->display, menu->focus_handler);

  g_cancellable_cancel (menu->cancellable);
  g_clear_object (&menu->cancellable);
  ooze_global_menu_cancel_bind_query (menu);
  ooze_global_menu_cancel_focus_sync (menu);
  ooze_global_menu_cancel_registrar_retry (menu);
  if (menu->session && menu->registrar_signal_id != 0)
    {
      g_dbus_connection_signal_unsubscribe (menu->session,
                                            menu->registrar_signal_id);
      menu->registrar_signal_id = 0;
    }

  ooze_global_menu_unwatch_window (menu);
  ooze_global_menu_clear_proxy (menu);
  g_clear_pointer (&menu->x11_launch_hint, g_free);
  g_clear_object (&menu->session);
  g_free (menu);
}

void
ooze_global_menu_set_wayland_appmenu (OozeGlobalMenu     *menu,
                                      OozeAppmenuWayland *wl_appmenu)
{
  if (!menu)
    return;
  menu->wl_appmenu = wl_appmenu;
}

void
ooze_global_menu_set_changed_callback (OozeGlobalMenu           *menu,
                                     OozeGlobalMenuChangedFunc callback,
                                     gpointer                user_data)
{
  g_return_if_fail (menu != NULL);
  menu->changed_cb = callback;
  menu->changed_data = user_data;
}

static gboolean
ooze_global_menu_window_is_minimized (MetaWindow *window)
{
  gboolean minimized = FALSE;

  if (!window)
    return FALSE;

  g_object_get (window, "minimized", &minimized, NULL);
  return minimized;
}

static MetaWindow *
ooze_global_menu_find_app_sibling (OozeGlobalMenu *menu,
                                 MetaWindow   *window)
{
  const char *bus;
  const char *app_id;
  GList *windows;
  GList *l;
  MetaWindow *best = NULL;

  if (!menu || !window)
    return NULL;

  bus = meta_window_get_gtk_unique_bus_name (window);
  app_id = meta_window_get_gtk_application_id (window);
  /* Only fall back to the currently bound bus when it belongs to this window. */
  if ((!bus || !*bus) && !app_id &&
      menu->bound_window == window && menu->bus_name)
    bus = menu->bus_name;

  windows = meta_display_list_all_windows (menu->display);
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *w = l->data;

      if (w == window)
        continue;
      if (meta_window_get_window_type (w) != META_WINDOW_NORMAL)
        continue;
      if (ooze_global_menu_window_is_minimized (w))
        continue;

      if (bus &&
          g_strcmp0 (bus, meta_window_get_gtk_unique_bus_name (w)) == 0)
        {
          best = w;
          break;
        }

      if (app_id &&
          g_strcmp0 (app_id, meta_window_get_gtk_application_id (w)) == 0)
        {
          best = w;
          break;
        }
    }
  g_list_free (windows);
  return best;
}

void
ooze_global_menu_sync_focus (OozeGlobalMenu *menu)
{
  MetaWindow *focus;

  g_return_if_fail (menu != NULL);

  focus = meta_display_get_focus_window (menu->display);

  /*
   * Clicking the panel often clears window focus. Keep the last app's
   * menubar/actions bound so Edit → Copy etc. still reach that app.
   */
  if (!focus)
    return;

  /*
   * Minimizing the focused window: prefer another visible window of the
   * same app (macOS-style). If none, keep the existing bind.
   */
  if (ooze_global_menu_window_is_minimized (focus))
    {
      MetaWindow *sibling = ooze_global_menu_find_app_sibling (menu, focus);

      if (sibling)
        focus = sibling;
      else
        return;
    }

  ooze_global_menu_bind_window (menu, focus);
}

void
ooze_global_menu_prepare_for_open (OozeGlobalMenu *menu)
{
  MetaWindow *window;

  g_return_if_fail (menu != NULL);

  /* Flush any pending focus debounce so the click path binds the real focus. */
  ooze_global_menu_flush_focus_sync (menu);

  window = meta_display_get_focus_window (menu->display);
  if (window && ooze_global_menu_window_is_minimized (window))
    {
      MetaWindow *sibling = ooze_global_menu_find_app_sibling (menu, window);

      window = sibling ? sibling : NULL;
    }
  if (!window)
    window = menu->bound_window;
  if (window && ooze_global_menu_window_is_minimized (window))
    {
      MetaWindow *sibling = ooze_global_menu_find_app_sibling (menu, window);

      if (sibling)
        window = sibling;
    }
  if (!window)
    window = menu->watched_window;
  if (window)
    ooze_global_menu_bind_window (menu, window);
}

gboolean
ooze_global_menu_take_pending_top (OozeGlobalMenu *menu,
                                 guint        *top_index_out)
{
  g_return_val_if_fail (menu != NULL, FALSE);

  if (!menu->pending_top_valid)
    return FALSE;

  if (top_index_out)
    *top_index_out = menu->pending_top_index;

  menu->pending_top_valid = FALSE;
  return TRUE;
}

void
ooze_global_menu_discard_pending (OozeGlobalMenu *menu)
{
  g_return_if_fail (menu != NULL);
  ooze_global_menu_clear_pending_submenu (menu);
}

gboolean
ooze_global_menu_has_app_menu (OozeGlobalMenu *menu)
{
  if (!menu)
    return FALSE;
  if (menu->dbusmenu && menu->n_dbus_tops > 0)
    return TRUE;
  return menu->menubar != NULL &&
         g_menu_model_get_n_items (menu->menubar) > 0;
}

gboolean
ooze_global_menu_has_dbusmenu (OozeGlobalMenu *menu)
{
  return menu && menu->dbusmenu && menu->n_dbus_tops > 0;
}

const char *
ooze_global_menu_get_x11_launch_hint (OozeGlobalMenu *menu)
{
  if (!menu || !menu->x11_launch_hint || !*menu->x11_launch_hint)
    return NULL;
  return menu->x11_launch_hint;
}

static gboolean
ooze_global_menu_window_is_spot (MetaWindow *window)
{
  const char *app_id;
  const char *wm_class;

  if (!window)
    return FALSE;

  app_id = meta_window_get_gtk_application_id (window);
  if (app_id && g_strcmp0 (app_id, "org.ooze.Spot") == 0)
    return TRUE;

  wm_class = meta_window_get_wm_class (window);
  if (wm_class &&
      (g_ascii_strcasecmp (wm_class, "spot") == 0 ||
       g_ascii_strcasecmp (wm_class, "org.ooze.Spot") == 0))
    return TRUE;

  return FALSE;
}

gboolean
ooze_global_menu_wants_shell_stubs (OozeGlobalMenu *menu)
{
  MetaWindow *focus;

  if (!menu || !menu->display)
    return TRUE;

  /* Bound foreign app menu → never Spot stubs. */
  if (ooze_global_menu_has_app_menu (menu))
    return FALSE;

  focus = meta_display_get_focus_window (menu->display);
  if (!focus)
    {
      /* Panel click clears focus; keep stubs only if last bind was Spot/none. */
      if (!menu->bound_window && !menu->watched_window)
        return TRUE;
      if (menu->watched_window)
        return ooze_global_menu_window_is_spot (menu->watched_window);
      return ooze_global_menu_window_is_spot (menu->bound_window);
    }

  return ooze_global_menu_window_is_spot (focus);
}

MetaWindow *
ooze_global_menu_get_fallback_window (OozeGlobalMenu *menu)
{
  MetaWindow *focus;

  if (!menu || !menu->display)
    return NULL;

  if (ooze_global_menu_has_app_menu (menu))
    return NULL;

  focus = meta_display_get_focus_window (menu->display);
  if (!focus)
    {
      /* Panel click clears focus; fall back to the last watched/bound app. */
      if (menu->watched_window &&
          !ooze_global_menu_window_is_spot (menu->watched_window))
        return menu->watched_window;
      if (menu->bound_window &&
          !ooze_global_menu_window_is_spot (menu->bound_window))
        return menu->bound_window;
      return NULL;
    }

  if (ooze_global_menu_window_is_spot (focus))
    return NULL;

  return focus;
}

guint
ooze_global_menu_get_n_top (OozeGlobalMenu *menu)
{
  gint n;

  if (!menu)
    return 0;

  if (menu->dbusmenu)
    {
      if (menu->n_dbus_tops > OOZE_GLOBAL_MENU_MAX_TOP)
        return OOZE_GLOBAL_MENU_MAX_TOP;
      return (guint) menu->n_dbus_tops;
    }

  if (!menu->menubar)
    return 0;

  n = g_menu_model_get_n_items (menu->menubar);
  if (n < 0)
    return 0;
  if ((guint) n > OOZE_GLOBAL_MENU_MAX_TOP)
    return OOZE_GLOBAL_MENU_MAX_TOP;
  return (guint) n;
}

/* Strip GTK mnemonic underscores (_F → F, __ → _). */
static char *
ooze_global_menu_clean_label (const char *raw)
{
  GString *out;
  const char *p;

  if (!raw || !*raw)
    return g_strdup ("");

  out = g_string_new (NULL);
  p = raw;
  while (*p)
    {
      if (p[0] == '_' && p[1] != '\0')
        {
          if (p[1] == '_')
            {
              g_string_append_c (out, '_');
              p += 2;
            }
          else
            {
              p++; /* skip mnemonic marker */
            }
          continue;
        }

      g_string_append_unichar (out, g_utf8_get_char (p));
      p = g_utf8_next_char (p);
    }

  return g_string_free (out, FALSE);
}

const char *
ooze_global_menu_get_top_label (OozeGlobalMenu *menu,
                              guint         index)
{
  g_autofree char *label = NULL;
  g_autofree char *clean = NULL;

  if (!menu || index >= ooze_global_menu_get_n_top (menu))
    return NULL;

  if (menu->dbusmenu)
    {
      if (!menu->dbus_tops[index].label || !*menu->dbus_tops[index].label)
        return NULL;
      return menu->dbus_tops[index].label;
    }

  if (!menu->menubar)
    return NULL;

  if (g_menu_model_get_item_attribute (menu->menubar, (int) index,
                                       G_MENU_ATTRIBUTE_LABEL, "s", &label))
    {
      static char *cache[OOZE_GLOBAL_MENU_MAX_TOP];

      clean = ooze_global_menu_clean_label (label);
      if (!clean || !*clean)
        return NULL;
      g_free (cache[index]);
      cache[index] = g_steal_pointer (&clean);
      return cache[index];
    }

  return NULL;
}

static int
ooze_global_menu_push_action (OozeGlobalMenu *menu,
                            const char   *action,
                            GVariant     *target)
{
  OozeGlobalMenuAction *slot;
  int id;

  menu->actions = g_renew (OozeGlobalMenuAction, menu->actions, menu->n_actions + 1);
  slot = &menu->actions[menu->n_actions];
  slot->action = g_strdup (action);
  slot->target = target ? g_variant_ref_sink (target) : NULL;
  id = OOZE_MENU_APP_ACTION_BASE + (int) menu->n_actions;
  menu->n_actions++;
  return id;
}

static void
ooze_global_menu_add_separator_row (GPtrArray *rows)
{
  OozeAquaMenuEntry *row;

  if (rows->len == 0)
    return;

  row = rows->pdata[rows->len - 1];
  if (row && row->label == NULL)
    return;

  row = g_new0 (OozeAquaMenuEntry, 1);
  row->label = NULL;
  row->action_id = 0;
  row->sensitive = FALSE;
  g_ptr_array_add (rows, row);
}

static void
ooze_global_menu_append_item (OozeGlobalMenu *menu,
                            GMenuModel   *model,
                            int           index,
                            GPtrArray    *rows,
                            const char   *prefix)
{
  OozeAquaMenuEntry *row;
  g_autofree char *raw_label = NULL;
  g_autofree char *clean = NULL;
  g_autofree char *action = NULL;
  g_autoptr (GVariant) target = NULL;
  GMenuModel *section;
  GMenuModel *submenu;

  section = g_menu_model_get_item_link (model, index, G_MENU_LINK_SECTION);
  if (section)
    {
      ooze_global_menu_add_separator_row (rows);
      ooze_global_menu_append_model (menu, section, rows, prefix);
      g_object_unref (section);
      return;
    }

  submenu = g_menu_model_get_item_link (model, index, G_MENU_LINK_SUBMENU);
  if (submenu)
    {
      g_autofree char *sub_prefix = NULL;

      if (g_menu_model_get_item_attribute (model, index,
                                           G_MENU_ATTRIBUTE_LABEL, "s",
                                           &raw_label))
        clean = ooze_global_menu_clean_label (raw_label);

      if (clean && *clean)
        {
          if (prefix && *prefix)
            sub_prefix = g_strdup_printf ("%s ▸ %s", prefix, clean);
          else
            sub_prefix = g_strdup (clean);
        }
      else if (prefix && *prefix)
        {
          sub_prefix = g_strdup (prefix);
        }

      ooze_global_menu_add_separator_row (rows);
      ooze_global_menu_append_model (menu, submenu, rows, sub_prefix);
      g_object_unref (submenu);
      return;
    }

  row = g_new0 (OozeAquaMenuEntry, 1);

  if (!g_menu_model_get_item_attribute (model, index,
                                        G_MENU_ATTRIBUTE_LABEL, "s",
                                        &raw_label))
    {
      row->label = NULL;
      row->action_id = 0;
      row->sensitive = FALSE;
      g_ptr_array_add (rows, row);
      return;
    }

  clean = ooze_global_menu_clean_label (raw_label);
  if (prefix && *prefix && clean && *clean)
    row->label = g_strdup_printf ("%s ▸ %s", prefix, clean);
  else
    row->label = g_steal_pointer (&clean);

  if (g_menu_model_get_item_attribute (model, index,
                                       G_MENU_ATTRIBUTE_ACTION, "s",
                                       &action))
    {
      target = g_menu_model_get_item_attribute_value (model, index,
                                                      G_MENU_ATTRIBUTE_TARGET,
                                                      NULL);
      row->action_id = ooze_global_menu_push_action (menu, action, target);
      row->sensitive = ooze_global_menu_action_sensitive (menu, action);
    }
  else
    {
      row->action_id = 0;
      row->sensitive = FALSE;
    }

  g_ptr_array_add (rows, row);
}

static void
ooze_global_menu_append_model (OozeGlobalMenu *menu,
                             GMenuModel   *model,
                             GPtrArray    *rows,
                             const char   *prefix)
{
  gint n;
  gint i;

  if (!model)
    return;

  n = g_menu_model_get_n_items (model);
  for (i = 0; i < n; i++)
    ooze_global_menu_append_item (menu, model, i, rows, prefix);
}

static void
ooze_global_menu_free_row (gpointer data)
{
  OozeAquaMenuEntry *row = data;

  if (!row)
    return;
  g_free ((char *) row->label);
  g_free (row);
}

static void
ooze_global_menu_append_dbusmenu_items (OozeGlobalMenu    *menu,
                                      OozeDbusmenuItem  *items,
                                      gsize            n,
                                      GPtrArray       *rows,
                                      const char      *prefix)
{
  gsize i;

  for (i = 0; i < n; i++)
    {
      OozeDbusmenuItem *item = &items[i];
      OozeAquaMenuEntry *row;
      g_autofree char *action = NULL;

      if (item->separator)
        {
          ooze_global_menu_add_separator_row (rows);
          continue;
        }

      if (item->has_children)
        {
          OozeDbusmenuItem *kids = NULL;
          gsize n_kids = 0;
          g_autofree char *sub_prefix = NULL;

          if (item->label && *item->label)
            {
              if (prefix && *prefix)
                sub_prefix = g_strdup_printf ("%s ▸ %s", prefix, item->label);
              else
                sub_prefix = g_strdup (item->label);
            }
          else if (prefix && *prefix)
            {
              sub_prefix = g_strdup (prefix);
            }

          ooze_global_menu_add_separator_row (rows);
          if (ooze_dbusmenu_get_children (menu->dbusmenu, item->id, &kids, &n_kids))
            {
              ooze_global_menu_append_dbusmenu_items (menu, kids, n_kids, rows,
                                                    sub_prefix);
              ooze_dbusmenu_items_free (kids, n_kids);
            }
          continue;
        }

      if (!item->label || !*item->label)
        continue;

      row = g_new0 (OozeAquaMenuEntry, 1);
      if (prefix && *prefix)
        row->label = g_strdup_printf ("%s ▸ %s", prefix, item->label);
      else
        row->label = g_strdup (item->label);

      action = g_strdup_printf ("dbusmenu:%d", item->id);
      row->action_id = ooze_global_menu_push_action (menu, action, NULL);
      row->sensitive = item->enabled;
      g_ptr_array_add (rows, row);
    }
}

gboolean
ooze_global_menu_fill_entries (OozeGlobalMenu     *menu,
                             guint             top_index,
                             OozeAquaMenuEntry **entries_out,
                             gsize            *n_entries_out)
{
  GMenuModel *section;
  gint i;
  g_autoptr (GPtrArray) rows = NULL;

  g_return_val_if_fail (menu != NULL, FALSE);
  g_return_val_if_fail (entries_out != NULL && n_entries_out != NULL, FALSE);

  ooze_global_menu_clear_entries (menu);
  /* Keep an in-flight submenu subscription when retrying the same top. */
  if (!(menu->pending_top_valid && menu->pending_top_index == top_index &&
        menu->pending_submenu))
    ooze_global_menu_clear_pending_submenu (menu);
  *entries_out = NULL;
  *n_entries_out = 0;

  if (top_index >= ooze_global_menu_get_n_top (menu))
    return FALSE;

  menu->suppress_changed++;
  rows = g_ptr_array_new_with_free_func (ooze_global_menu_free_row);

  if (menu->dbusmenu)
    {
      OozeDbusmenuItem *kids = NULL;
      gsize n_kids = 0;
      int parent_id = menu->dbus_tops[top_index].id;

      ooze_dbusmenu_opened (menu->dbusmenu, parent_id);

      if (ooze_dbusmenu_get_children (menu->dbusmenu, parent_id, &kids, &n_kids))
        {
          ooze_global_menu_append_dbusmenu_items (menu, kids, n_kids, rows, NULL);
          ooze_dbusmenu_items_free (kids, n_kids);
        }

      menu->suppress_changed--;

      /* Children may still be in flight; the changed callback re-opens. */
      if (rows->len == 0)
        return FALSE;

      menu->n_entries = rows->len;
      menu->entries = g_new0 (OozeAquaMenuEntry, menu->n_entries);
      {
        gsize j;

        for (j = 0; j < rows->len; j++)
          {
            OozeAquaMenuEntry *src = rows->pdata[j];

            menu->entries[j] = *src;
            src->label = NULL;
          }
      }

      *entries_out = menu->entries;
      *n_entries_out = menu->n_entries;
      return TRUE;
    }

  if (!menu->menubar)
    {
      menu->suppress_changed--;
      return FALSE;
    }

  /*
   * Reading D-Bus menu links can emit items-changed synchronously. That
   * used to rebuild the panel menubar and destroy the click anchor before
   * the popup could open — so only the Ooze button appeared to work.
   *
   * Prefer the primed submenu cache so GDBusMenuModel's Start() is not
   * cancelled by dropping the link ref between retries.
   */
  if (top_index < OOZE_GLOBAL_MENU_MAX_TOP && menu->submenu_models[top_index])
    section = g_object_ref (menu->submenu_models[top_index]);
  else
    {
      section = g_menu_model_get_item_link (menu->menubar, (int) top_index,
                                            G_MENU_LINK_SUBMENU);
      if (!section)
        section = g_menu_model_get_item_link (menu->menubar, (int) top_index,
                                              G_MENU_LINK_SECTION);
      if (section && top_index < OOZE_GLOBAL_MENU_MAX_TOP &&
          !menu->submenu_models[top_index])
        {
          menu->submenu_models[top_index] = g_object_ref (section);
          (void) g_menu_model_get_n_items (menu->submenu_models[top_index]);
          menu->submenu_handlers[top_index] =
            g_signal_connect (menu->submenu_models[top_index], "items-changed",
                              G_CALLBACK (on_cached_submenu_items_changed),
                              menu);
        }
    }

  if (!section)
    {
      g_autofree char *action = NULL;
      OozeAquaMenuEntry *one;
      const char *label;

      label = ooze_global_menu_get_top_label (menu, top_index);
      if (!g_menu_model_get_item_attribute (menu->menubar, (int) top_index,
                                            G_MENU_ATTRIBUTE_ACTION, "s", &action))
        {
          menu->suppress_changed--;
          return FALSE;
        }

      one = g_new0 (OozeAquaMenuEntry, 1);
      one->label = g_strdup (label ? label : "Item");
      one->action_id = ooze_global_menu_push_action (menu, action, NULL);
      one->sensitive = ooze_global_menu_action_sensitive (menu, action);
      g_ptr_array_add (rows, one);
    }
  else
    {
      ooze_global_menu_append_model (menu, section, rows, NULL);

      /*
       * GDBusMenuModel submenus often report 0 items until the first
       * Subscribe completes. Watch for population and ask the panel to
       * reopen — never fall through to shell Spot stubs on first paint.
       */
      if (rows->len == 0)
        {
          if (menu->pending_submenu != section)
            {
              ooze_global_menu_clear_pending_submenu (menu);
              menu->pending_submenu = g_object_ref (section);
              menu->pending_submenu_handler =
                g_signal_connect (section, "items-changed",
                                  G_CALLBACK (on_pending_submenu_items_changed),
                                  menu);
            }
          menu->pending_top_index = top_index;
          menu->pending_top_valid = TRUE;
          g_object_unref (section);
          menu->suppress_changed--;
          return FALSE;
        }

      g_object_unref (section);
    }

  menu->suppress_changed--;

  if (rows->len == 0)
    return FALSE;

  menu->n_entries = rows->len;
  menu->entries = g_new0 (OozeAquaMenuEntry, menu->n_entries);
  for (i = 0; i < (gint) rows->len; i++)
    {
      OozeAquaMenuEntry *src = rows->pdata[i];

      menu->entries[i] = *src;
      /* Steal label ownership into menu->entries. */
      src->label = NULL;
    }

  *entries_out = menu->entries;
  *n_entries_out = menu->n_entries;
  return TRUE;
}

static GActionGroup *
ooze_global_menu_resolve_group (OozeGlobalMenu *menu,
                              const char   *action,
                              const char  **bare_name);

static gboolean
ooze_global_menu_action_sensitive (OozeGlobalMenu *menu,
                                 const char   *action)
{
  GActionGroup *group;
  const char *bare;

  if (!action || !*action)
    return FALSE;

  group = ooze_global_menu_resolve_group (menu, action, &bare);
  if (!group || !bare || !*bare)
    return TRUE;

  if (!g_action_group_has_action (group, bare))
    return TRUE;

  return g_action_group_get_action_enabled (group, bare);
}

static GActionGroup *
ooze_global_menu_resolve_group (OozeGlobalMenu *menu,
                              const char   *action,
                              const char  **bare_name)
{
  const char *dot;

  *bare_name = action;

  if (!action)
    return NULL;

  dot = strchr (action, '.');
  if (dot && g_str_has_prefix (action, "win.") && menu->win_actions)
    {
      *bare_name = dot + 1;
      return menu->win_actions;
    }
  if (dot && g_str_has_prefix (action, "app.") && menu->app_actions)
    {
      *bare_name = dot + 1;
      return menu->app_actions;
    }

  /* GTK3 / Inkscape sometimes use other prefixes — strip first segment. */
  if (dot && dot[1] != '\0')
    {
      const char *bare = dot + 1;

      if (menu->win_actions && g_action_group_has_action (menu->win_actions, bare))
        {
          *bare_name = bare;
          return menu->win_actions;
        }
      if (menu->app_actions && g_action_group_has_action (menu->app_actions, bare))
        {
          *bare_name = bare;
          return menu->app_actions;
        }
    }

  if (menu->win_actions &&
      g_action_group_has_action (menu->win_actions, action))
    return menu->win_actions;
  if (menu->app_actions &&
      g_action_group_has_action (menu->app_actions, action))
    return menu->app_actions;

  return menu->win_actions ? menu->win_actions : menu->app_actions;
}

static gboolean
ooze_global_menu_try_activate (GActionGroup *group,
                             const char   *name,
                             GVariant     *target)
{
  if (!group || !name || !*name)
    return FALSE;

  g_action_group_activate_action (group, name, target);
  return TRUE;
}

static gboolean
ooze_global_menu_dispatch_pending_activate (gpointer user_data)
{
  OozeGlobalMenu *menu = user_data;
  GActionGroup *group;
  const char *bare;
  g_autofree char *action = NULL;
  g_autoptr (GVariant) target = NULL;

  menu->activate_idle = 0;
  action = g_steal_pointer (&menu->pending_action);
  target = g_steal_pointer (&menu->pending_target);
  if (!action)
    return G_SOURCE_REMOVE;

  if (g_str_has_prefix (action, "dbusmenu:"))
    {
      int id = (int) g_ascii_strtoll (action + strlen ("dbusmenu:"), NULL, 10);

      if (menu->dbusmenu)
        ooze_dbusmenu_click (menu->dbusmenu, id);
      return G_SOURCE_REMOVE;
    }

  if (menu->bound_window && !menu->win_actions &&
      g_str_has_prefix (action, "win."))
    ooze_global_menu_bind_window (menu, menu->bound_window);

  ooze_global_menu_kick_actions (menu->win_actions);
  ooze_global_menu_kick_actions (menu->app_actions);

  group = ooze_global_menu_resolve_group (menu, action, &bare);
  if (group && bare && ooze_global_menu_try_activate (group, bare, target))
    return G_SOURCE_REMOVE;

  if (menu->win_actions &&
      ooze_global_menu_try_activate (menu->win_actions, action, target))
    return G_SOURCE_REMOVE;
  if (menu->app_actions &&
      ooze_global_menu_try_activate (menu->app_actions, action, target))
    return G_SOURCE_REMOVE;

  /* Strip prefix one more time for GTK3 / Inkscape oddities. */
  if (bare && strchr (action, '.') && menu->win_actions &&
      ooze_global_menu_try_activate (menu->win_actions, bare, target))
    return G_SOURCE_REMOVE;
  if (bare && strchr (action, '.') && menu->app_actions &&
      ooze_global_menu_try_activate (menu->app_actions, bare, target))
    return G_SOURCE_REMOVE;

  g_warning ("Ooze global menu: could not activate '%s'", action);
  return G_SOURCE_REMOVE;
}

void
ooze_global_menu_activate (OozeGlobalMenu *menu,
                         int           action_id)
{
  int index;
  OozeGlobalMenuAction *slot;

  if (!menu || action_id < OOZE_MENU_APP_ACTION_BASE)
    return;

  index = action_id - OOZE_MENU_APP_ACTION_BASE;
  if (index < 0 || (gsize) index >= menu->n_actions)
    return;

  slot = &menu->actions[index];
  if (!slot->action)
    return;

  /* Panel click clears focus; restore the app before sending the action. */
  if (menu->bound_window)
    meta_window_activate (menu->bound_window, clutter_get_current_event_time ());

  ooze_global_menu_clear_pending_activate (menu);
  menu->pending_action = g_strdup (slot->action);
  menu->pending_target = slot->target ? g_variant_ref (slot->target) : NULL;

  /* Unity-style: give the client a short beat to accept focus + action map. */
  menu->activate_idle =
    g_timeout_add (40, ooze_global_menu_dispatch_pending_activate, menu);
}

MetaWindow *
ooze_global_menu_get_bound_window (OozeGlobalMenu *menu)
{
  return menu ? menu->bound_window : NULL;
}
