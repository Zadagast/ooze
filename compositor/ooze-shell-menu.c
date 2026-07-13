#include "ooze-shell-menu.h"
#include "ooze-plugin-priv.h"
#include "ooze-global-menu.h"
#include "ooze-dock-shell.h"
#include "ooze-theme.h"

#include <meta/display.h>
#include <meta/meta-plugin.h>
#include <meta/meta-context.h>
#include <meta/window.h>
#include <clutter/clutter.h>
#include <gio/gio.h>

/* ── Shell menu bar labels (File / Edit / View / Go / Window / Help) ─────── */

const char *const ooze_shell_menu_bar_items[] = {
  "File",
  "Edit",
  "View",
  "Go",
  "Window",
  "Help",
};
const gsize ooze_shell_menu_bar_n_items = G_N_ELEMENTS (ooze_shell_menu_bar_items);

/* ── logind ──────────────────────────────────────────────────────────────── */

static void
ooze_plugin_menu_logind_call (const char *method)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GError) error = NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.login1",
                                         "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager",
                                         NULL,
                                         &error);
  if (!proxy)
    {
      g_warning ("Ooze menu: logind unavailable: %s", error->message);
      return;
    }

  g_dbus_proxy_call_sync (proxy,
                          method,
                          g_variant_new ("(b)", TRUE),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          &error);
  if (error)
    g_warning ("Ooze menu: %s failed: %s", method, error->message);
}

/* ── Action dispatch ─────────────────────────────────────────────────────── */

void
ooze_shell_menu_action (gpointer user_data, int action_id)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  MetaDisplay *display;
  MetaWindow *window;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  window = meta_display_get_focus_window (display);

  if (action_id >= OOZE_MENU_APP_ACTION_BASE)
    {
      if (plugin->global_menu)
        ooze_global_menu_activate (plugin->global_menu, action_id);
      return;
    }

  switch (action_id)
    {
    case OOZE_MENU_FILE_NEW_SPOT:
      ooze_dock_launch_spot (plugin->context);
      break;

    case OOZE_MENU_FILE_CLOSE:
      if (window)
        meta_window_delete (window, clutter_get_current_event_time ());
      break;

    case OOZE_MENU_GO_COMPUTER:
      ooze_dock_launch_spot_path (plugin->context, "/");
      break;

    case OOZE_MENU_GO_HOME:
      ooze_dock_launch_spot_path (plugin->context, g_get_home_dir ());
      break;

    case OOZE_MENU_GO_DESKTOP:
      ooze_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP));
      break;

    case OOZE_MENU_GO_DOCUMENTS:
      ooze_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
      break;

    case OOZE_MENU_GO_DOWNLOADS:
      ooze_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
      break;

    case OOZE_MENU_GO_APPLICATIONS:
      ooze_dock_launch_pak (plugin->context);
      break;

    case OOZE_MENU_WINDOW_MINIMIZE:
      if (window)
        meta_window_minimize (window);
      break;

    case OOZE_MENU_WINDOW_ZOOM:
      if (window)
        {
          if (meta_window_is_maximized (window))
            meta_window_unmaximize (window);
          else
            meta_window_maximize (window);
        }
      break;

    case OOZE_MENU_WINDOW_BRING_ALL:
      {
        MetaWindow *ref;
        GList *windows;
        GList *l;
        const char *app_id;
        const char *bus;
        const char *wm_class;
        MetaWindow *focus_target = NULL;

        ref = window;
        if (!ref && plugin->global_menu)
          ref = ooze_global_menu_get_bound_window (plugin->global_menu);
        if (!ref)
          break;

        app_id = meta_window_get_gtk_application_id (ref);
        bus = meta_window_get_gtk_unique_bus_name (ref);
        wm_class = meta_window_get_wm_class (ref);

        windows = meta_display_list_all_windows (display);
        for (l = windows; l != NULL; l = l->next)
          {
            MetaWindow *w = l->data;
            gboolean same_app = FALSE;

            if (meta_window_get_window_type (w) != META_WINDOW_NORMAL)
              continue;

            if (bus &&
                g_strcmp0 (bus, meta_window_get_gtk_unique_bus_name (w)) == 0)
              same_app = TRUE;
            else if (app_id &&
                     g_strcmp0 (app_id,
                                meta_window_get_gtk_application_id (w)) == 0)
              same_app = TRUE;
            else if (wm_class &&
                     g_strcmp0 (wm_class, meta_window_get_wm_class (w)) == 0)
              same_app = TRUE;

            if (!same_app)
              continue;

            meta_window_unminimize (w);
            meta_window_raise (w);
            focus_target = w;
          }
        g_list_free (windows);

        if (focus_target)
          meta_window_activate (focus_target, clutter_get_current_event_time ());
        else
          meta_window_activate (ref, clutter_get_current_event_time ());
      }
      break;

    case OOZE_MENU_OOZE_RESTART:
      ooze_plugin_menu_logind_call ("Reboot");
      break;

    case OOZE_MENU_OOZE_SHUTDOWN:
      ooze_plugin_menu_logind_call ("PowerOff");
      break;

    case OOZE_MENU_OOZE_LOGOUT:
      g_print ("Ooze: ending session\n");
      meta_context_terminate (plugin->context);
      break;

    case OOZE_MENU_OOZE_APPEARANCE:
      ooze_theme_toggle (ooze_theme_get_default ());
      break;

    case OOZE_MENU_HELP_ABOUT:
      ooze_dock_launch_about (plugin->context);
      break;

    default:
      if (action_id >= OOZE_MENU_WINDOW_FOCUS_BASE)
        {
          GList *windows;
          GList *l;
          int index = action_id - OOZE_MENU_WINDOW_FOCUS_BASE;

          windows = meta_display_list_all_windows (display);
          for (l = windows; l != NULL; l = l->next)
            {
              MetaWindow *w = l->data;

              if (meta_window_get_window_type (w) != META_WINDOW_NORMAL)
                continue;

              if (index == 0)
                {
                  meta_window_focus (w, clutter_get_current_event_time ());
                  break;
                }

              index--;
            }
          g_list_free (windows);
        }
      break;
    }
}
