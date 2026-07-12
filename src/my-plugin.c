#include "my-plugin.h"
#include "my-aqua-draw.h"
#include "my-aqua-menu.h"
#include "my-global-menu.h"
#include "my-desktop-icons.h"
#include "my-dock.h"
#include "my-magic-lamp.h"
#include "my-theme.h"
#include "my-window.h"

#include "../common/aqua-chrome.h"
#include "../common/ooze-font.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-background-actor.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background.h>
#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <meta/meta-window-actor.h>
#include <meta/meta-workspace-manager.h>
#include <meta/prefs.h>
#include <meta/window.h>
#include <meta/workspace.h>
#include <mtk/mtk.h>

#include <gio/gio.h>
#include <glib.h>
#include <string.h>
#include <time.h>

#define PANEL_HEIGHT          22.0f
#define OOZE_BUTTON_MARGIN    4.0f
#define MENU_ITEM_GAP         18.0f
#define MENU_BAR_HIT_HEIGHT   PANEL_HEIGHT

#define AQUA_DOCK_PLATE_H     58.0f
#define AQUA_DOCK_ITEM_SIZE   48.0f
#define AQUA_DOCK_PADDING     14.0f
#define AQUA_DOCK_ITEM_GAP    6.0f
#define AQUA_DOCK_BOTTOM_GAP  8.0f

typedef struct
{
  ClutterActor *titlebar;
  ClutterActor *title_label;
  int last_titlebar_width;
  char *last_title;
  gulong position_changed_id;
} MyWindowChrome;

struct _MyPlugin
{
  MetaPlugin parent_instance;

  ClutterActor *background_group;
  ClutterActor *panel;
  ClutterActor *menu_icon;
  ClutterActor *menu_bar_labels[MY_GLOBAL_MENU_MAX_TOP];
  guint         n_menu_bar_labels;
  gboolean      menu_bar_from_app;
  ClutterActor *clock_label;
  ClutterActor *aqua_dock;
  ClutterActor *aqua_dock_plate;
  ClutterActor *aqua_dock_icons;
  ClutterActor *tile_preview;
  MetaMonitorManager *monitor_manager;
  MetaContext *context;
  MyAquaMenu *menu_popup;
  MyGlobalMenu *global_menu;
  gulong monitors_changed_handler;
  gulong workspace_added_handler;
  guint clock_timer;
  gulong stage_key_handler;
  int last_panel_width;
  int last_dock_plate_width;
};

G_DEFINE_TYPE (MyPlugin, my_plugin, META_TYPE_PLUGIN)

static const char *menu_bar_items[] = {
  "File",
  "Edit",
  "View",
  "Go",
  "Window",
  "Help",
};

typedef enum
{
  MY_MENU_FILE_NEW_SPOT = 1,
  MY_MENU_FILE_CLOSE,
  MY_MENU_EDIT_UNDO,
  MY_MENU_EDIT_CUT,
  MY_MENU_EDIT_COPY,
  MY_MENU_EDIT_PASTE,
  MY_MENU_EDIT_SELECT_ALL,
  MY_MENU_VIEW_ICONS,
  MY_MENU_VIEW_LIST,
  MY_MENU_VIEW_COLUMNS,
  MY_MENU_GO_COMPUTER,
  MY_MENU_GO_HOME,
  MY_MENU_GO_DESKTOP,
  MY_MENU_GO_DOCUMENTS,
  MY_MENU_GO_DOWNLOADS,
  MY_MENU_GO_APPLICATIONS,
  MY_MENU_WINDOW_MINIMIZE,
  MY_MENU_WINDOW_ZOOM,
  MY_MENU_WINDOW_BRING_ALL,
  MY_MENU_WINDOW_FOCUS_BASE = 100,
  MY_MENU_HELP_ABOUT = 200,
  MY_MENU_OOZE_RESTART    = 1000,
  MY_MENU_OOZE_SHUTDOWN,
  MY_MENU_OOZE_LOGOUT,
  MY_MENU_OOZE_APPEARANCE,
} MyMenuAction;

static void my_plugin_menu_action (gpointer user_data, int action_id);
static void my_plugin_show_bar_menu (MyPlugin *plugin, gsize menu_index, ClutterActor *anchor);
static void my_plugin_rebuild_menu_bar (MyPlugin *plugin);
static void my_plugin_on_global_menu_changed (gpointer user_data);
static ClutterActor *my_plugin_create_menu_bar_item (ClutterActor *ref_actor,
                                                     const char   *font_desc,
                                                     const char   *text,
                                                     gfloat        r,
                                                     gfloat        g,
                                                     gfloat        b);
static void my_plugin_layout_menu_labels (MyPlugin *plugin);
static MetaWindow *my_plugin_get_focus_window (MyPlugin *plugin);

static MetaWindow *
my_plugin_get_focus_window (MyPlugin *plugin)
{
  MetaDisplay *display;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  return meta_display_get_focus_window (display);
}

static void
my_plugin_menu_logind_call (const char *method)
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

static void
my_plugin_menu_action (gpointer user_data,
                       int      action_id)
{
  MyPlugin *plugin = MY_PLUGIN (user_data);
  MetaWindow *window;

  window = my_plugin_get_focus_window (plugin);

  if (action_id >= MY_MENU_APP_ACTION_BASE)
    {
      if (plugin->global_menu)
        my_global_menu_activate (plugin->global_menu, action_id);
      return;
    }

  switch (action_id)
    {
    case MY_MENU_FILE_NEW_SPOT:
      my_dock_launch_spot (plugin->context);
      break;

    case MY_MENU_FILE_CLOSE:
      if (window)
        meta_window_delete (window, clutter_get_current_event_time ());
      break;

    case MY_MENU_GO_COMPUTER:
      my_dock_launch_spot_path (plugin->context, "/");
      break;

    case MY_MENU_GO_HOME:
      my_dock_launch_spot_path (plugin->context, g_get_home_dir ());
      break;

    case MY_MENU_GO_DESKTOP:
      my_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP));
      break;

    case MY_MENU_GO_DOCUMENTS:
      my_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS));
      break;

    case MY_MENU_GO_DOWNLOADS:
      my_dock_launch_spot_path (plugin->context,
                                g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
      break;

    case MY_MENU_GO_APPLICATIONS:
      my_dock_launch_pak (plugin->context);
      break;

    case MY_MENU_WINDOW_MINIMIZE:
      if (window)
        meta_window_minimize (window);
      break;

    case MY_MENU_WINDOW_ZOOM:
      if (window)
        {
          if (meta_window_is_maximized (window))
            meta_window_unmaximize (window);
          else
            meta_window_maximize (window);
        }
      break;

    case MY_MENU_WINDOW_BRING_ALL:
      {
        MetaDisplay *display;
        GList *windows;
        GList *l;

        display = meta_plugin_get_display (META_PLUGIN (plugin));
        windows = meta_display_list_all_windows (display);
        for (l = windows; l != NULL; l = l->next)
          {
            MetaWindow *w = l->data;

            if (meta_window_get_window_type (w) == META_WINDOW_NORMAL)
              meta_window_unminimize (w);
          }
        g_list_free (windows);
      }
      break;

    case MY_MENU_OOZE_RESTART:
      my_plugin_menu_logind_call ("Reboot");
      break;

    case MY_MENU_OOZE_SHUTDOWN:
      my_plugin_menu_logind_call ("PowerOff");
      break;

    case MY_MENU_OOZE_LOGOUT:
      g_print ("Ooze: ending session\n");
      meta_context_terminate (plugin->context);
      break;

    case MY_MENU_OOZE_APPEARANCE:
      my_theme_toggle (my_theme_get_default ());
      break;

    case MY_MENU_HELP_ABOUT:
      g_print ("Ooze Desktop — a macOS-inspired Wayland compositor\n");
      break;

    default:
      if (action_id >= MY_MENU_WINDOW_FOCUS_BASE)
        {
          MetaDisplay *display;
          GList *windows;
          GList *l;
          int index = action_id - MY_MENU_WINDOW_FOCUS_BASE;

          display = meta_plugin_get_display (META_PLUGIN (plugin));
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

static gboolean
my_plugin_panel_child_is_fixed_chrome (MyPlugin     *plugin,
                                       ClutterActor *child)
{
  return child == plugin->menu_icon ||
         child == plugin->clock_label;
}

static gboolean
on_menu_bar_pressed (ClutterActor *actor,
                     ClutterEvent *event,
                     MyPlugin    *plugin)
{
  gsize menu_index;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  for (menu_index = 0; menu_index < plugin->n_menu_bar_labels; menu_index++)
    {
      if (plugin->menu_bar_labels[menu_index] == actor)
        {
          my_plugin_show_bar_menu (plugin, menu_index, actor);
          return CLUTTER_EVENT_STOP;
        }
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
on_ooze_button_pressed (ClutterActor *actor,
                        ClutterEvent *event,
                        MyPlugin    *plugin)
{
  g_autofree char *logout_label = NULL;
  gboolean         dark;
  /*
   * Ooze menu layout:
   *   Switch to Dark / Light Mode
   *   ──────────────────────────
   *   Restart...
   *   Shut Down...
   *   ──────────────────────────
   *   Log Out <user>...
   */
  MyAquaMenuEntry entries[] = {
    { NULL,            MY_MENU_OOZE_APPEARANCE, TRUE  }, /* [0] filled below */
    { NULL,            0,                       FALSE }, /* [1] separator    */
    { "Restart...",    MY_MENU_OOZE_RESTART,    TRUE  }, /* [2]              */
    { "Shut Down...",  MY_MENU_OOZE_SHUTDOWN,   TRUE  }, /* [3]              */
    { NULL,            0,                       FALSE }, /* [4] separator    */
    { NULL,            MY_MENU_OOZE_LOGOUT,     TRUE  }, /* [5] filled below */
  };

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  dark = my_theme_is_dark (my_theme_get_default ());
  entries[0].label = dark ? "Switch to Light Mode" : "Switch to Dark Mode";

  logout_label = g_strdup_printf ("Log Out %s...", g_get_user_name ());
  entries[5].label = logout_label;

  my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                  actor,
                                  entries,
                                  G_N_ELEMENTS (entries));
  return CLUTTER_EVENT_STOP;
}

static void
my_plugin_show_bar_menu (MyPlugin     *plugin,
                         gsize         menu_index,
                         ClutterActor *anchor)
{
  MetaWindow *focus;
  gboolean has_focus;
  MyAquaMenuEntry *app_entries = NULL;
  gsize n_app_entries = 0;

  if (plugin->menu_bar_from_app &&
      plugin->global_menu &&
      my_global_menu_fill_entries (plugin->global_menu,
                                   (guint) menu_index,
                                   &app_entries,
                                   &n_app_entries) &&
      n_app_entries > 0)
    {
      my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                      anchor,
                                      app_entries,
                                      n_app_entries);
      return;
    }

  focus = my_plugin_get_focus_window (plugin);
  has_focus = focus != NULL;

  switch (menu_index)
    {
    case 0:
      {
        MyAquaMenuEntry entries[] = {
          { "New Finder Window", MY_MENU_FILE_NEW_SPOT, TRUE },
          { "Close Window", MY_MENU_FILE_CLOSE, has_focus },
        };
        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        entries,
                                        G_N_ELEMENTS (entries));
      }
      break;

    case 1:
      {
        MyAquaMenuEntry entries[] = {
          { "Undo", MY_MENU_EDIT_UNDO, FALSE },
          { "Cut", MY_MENU_EDIT_CUT, FALSE },
          { "Copy", MY_MENU_EDIT_COPY, FALSE },
          { "Paste", MY_MENU_EDIT_PASTE, FALSE },
          { NULL, 0, FALSE },
          { "Select All", MY_MENU_EDIT_SELECT_ALL, FALSE },
        };
        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        entries,
                                        G_N_ELEMENTS (entries));
      }
      break;

    case 2:
      {
        MyAquaMenuEntry entries[] = {
          { "as Icons", MY_MENU_VIEW_ICONS, FALSE },
          { "as List", MY_MENU_VIEW_LIST, FALSE },
          { "as Columns", MY_MENU_VIEW_COLUMNS, FALSE },
        };
        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        entries,
                                        G_N_ELEMENTS (entries));
      }
      break;

    case 3:
      {
        MyAquaMenuEntry entries[] = {
          { "Computer", MY_MENU_GO_COMPUTER, TRUE },
          { "Home", MY_MENU_GO_HOME, TRUE },
          { "Desktop", MY_MENU_GO_DESKTOP, g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP) != NULL },
          { "Documents", MY_MENU_GO_DOCUMENTS, g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS) != NULL },
          { "Downloads", MY_MENU_GO_DOWNLOADS, g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD) != NULL },
          { NULL, 0, FALSE },
          { "Applications", MY_MENU_GO_APPLICATIONS, TRUE },
        };
        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        entries,
                                        G_N_ELEMENTS (entries));
      }
      break;

    case 4:
      {
        MetaDisplay *display;
        GList *windows;
        GList *l;
        g_autoptr (GPtrArray) entries = g_ptr_array_new ();
        MyAquaMenuEntry header[] = {
          { "Minimize", MY_MENU_WINDOW_MINIMIZE, has_focus },
          { "Zoom", MY_MENU_WINDOW_ZOOM, has_focus },
          { "Bring All to Front", MY_MENU_WINDOW_BRING_ALL, TRUE },
        };
        int focus_index = MY_MENU_WINDOW_FOCUS_BASE;

        for (gsize i = 0; i < G_N_ELEMENTS (header); i++)
          g_ptr_array_add (entries, &header[i]);

        display = meta_plugin_get_display (META_PLUGIN (plugin));
        windows = meta_display_list_all_windows (display);
        if (windows)
          {
            MyAquaMenuEntry separator = { NULL, 0, FALSE };
            g_ptr_array_add (entries, &separator);
          }

        for (l = windows; l != NULL; l = l->next)
          {
            MetaWindow *w = l->data;
            const char *title;
            MyAquaMenuEntry *item;

            if (meta_window_get_window_type (w) != META_WINDOW_NORMAL)
              continue;

            title = meta_window_get_title (w);
            if (!title || title[0] == '\0')
              title = "Untitled";

            item = g_new0 (MyAquaMenuEntry, 1);
            item->label = title;
            item->action_id = focus_index++;
            item->sensitive = TRUE;
            g_ptr_array_add (entries, item);
          }
        g_list_free (windows);

        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        (MyAquaMenuEntry *) entries->pdata,
                                        entries->len);

        for (guint i = G_N_ELEMENTS (header) + 1; i < entries->len; i++)
          g_free (entries->pdata[i]);
      }
      break;

    case 5:
      {
        MyAquaMenuEntry entries[] = {
          { "About Ooze", MY_MENU_HELP_ABOUT, TRUE },
        };
        my_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                        anchor,
                                        entries,
                                        G_N_ELEMENTS (entries));
      }
      break;

    default:
      break;
    }
}

static void my_plugin_raise_chrome (MyPlugin *plugin, MetaDisplay *display);

static void my_plugin_update_clock (MyPlugin *plugin);
static void my_plugin_update_layout (MyPlugin *plugin, MetaDisplay *display);
static void my_plugin_on_monitors_changed (MetaMonitorManager *monitor_manager,
                                           MyPlugin *plugin);
static void my_plugin_schedule_window_chrome_sync (MetaWindowActor *actor);
static void my_plugin_sync_window_chrome (MetaWindowActor *actor);
static void my_plugin_refresh_ooze_button (MyPlugin *plugin);
static void my_plugin_layout_menu_labels (MyPlugin *plugin);
static void my_plugin_set_chrome_visible (MetaWindowActor *actor, gboolean visible);
static gboolean my_plugin_window_is_minimized (MetaWindow *window);

static ClutterActor *
my_plugin_create_text_label (ClutterActor *ref_actor,
                             const char   *font_desc,
                             const char   *text,
                             gfloat        r,
                             gfloat        g,
                             gfloat        b)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  content = my_aqua_text_content (ref_actor,
                                  font_desc,
                                  text,
                                  r, g, b,
                                  &width,
                                  &height);
  actor = clutter_actor_new ();
  if (!content)
    {
      clutter_actor_set_size (actor, 1.0f, 1.0f);
      return actor;
    }

  my_aqua_actor_set_content (actor,
                             g_steal_pointer (&content),
                             width,
                             height);

  return actor;
}

static void
my_plugin_set_text_label (ClutterActor *actor,
                          const char   *font_desc,
                          const char   *text,
                          gfloat        r,
                          gfloat        g,
                          gfloat        b)
{
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  content = my_aqua_text_content (actor,
                                  font_desc,
                                  text,
                                  r, g, b,
                                  &width,
                                  &height);
  if (!content)
    return;

  my_aqua_actor_set_content (actor,
                             g_steal_pointer (&content),
                             width,
                             height);
}

static void
my_plugin_refresh_panel_texture (MyPlugin *plugin,
                                 int       width)
{
  g_autoptr (ClutterContent) content = NULL;

  if (!plugin->panel || width < 1)
    return;

  if (width == plugin->last_panel_width)
    return;

  content = my_aqua_pinstripe_content (plugin->panel, width, (int) PANEL_HEIGHT);
  if (content)
    my_aqua_actor_set_content (plugin->panel,
                               g_steal_pointer (&content),
                               width,
                               (int) PANEL_HEIGHT);

  plugin->last_panel_width = width;
}

static void
my_plugin_refresh_dock_plate (MyPlugin *plugin,
                                int       plate_width)
{
  g_autoptr (ClutterContent) content = NULL;

  if (!plugin->aqua_dock_plate || plate_width < 1)
    return;

  if (plate_width == plugin->last_dock_plate_width)
    return;

  content = my_aqua_dock_plate_content (plugin->aqua_dock_plate,
                                        plate_width,
                                        (int) AQUA_DOCK_PLATE_H);
  if (content)
    my_aqua_actor_set_content (plugin->aqua_dock_plate,
                               g_steal_pointer (&content),
                               plate_width,
                               (int) AQUA_DOCK_PLATE_H);

  plugin->last_dock_plate_width = plate_width;
}

static void
my_plugin_refresh_theme (MyPlugin *plugin)
{
  MetaDisplay *display;
  GList *windows;
  GList *l;

  if (!plugin->panel)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));

  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;

  my_plugin_refresh_ooze_button (plugin);
  my_plugin_rebuild_menu_bar (plugin);

  my_plugin_update_clock (plugin);
  my_plugin_update_layout (plugin, display);

  if (plugin->monitor_manager)
    my_plugin_on_monitors_changed (plugin->monitor_manager, plugin);

  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      MetaWindowActor *actor;
      MyWindowChrome *chrome;

      actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
      if (!actor || meta_window_actor_is_destroyed (actor))
        continue;

      chrome = g_object_get_data (G_OBJECT (actor), "my-window-chrome");
      if (!chrome)
        continue;

      chrome->last_titlebar_width = 0;
      g_clear_pointer (&chrome->last_title, g_free);
      my_plugin_schedule_window_chrome_sync (actor);
    }
  g_list_free (windows);
}

static void
my_plugin_on_theme_changed (gpointer user_data)
{
  my_plugin_refresh_theme (MY_PLUGIN (user_data));
}

static void
my_plugin_cancel_window_idle_ops (MetaWindowActor *actor)
{
  gpointer sync_id;
  gpointer map_id;

  sync_id = g_object_get_data (G_OBJECT (actor), "my-chrome-sync-id");
  if (sync_id)
    {
      g_source_remove (GPOINTER_TO_UINT (sync_id));
      g_object_set_data (G_OBJECT (actor), "my-chrome-sync-id", NULL);
    }

  map_id = g_object_get_data (G_OBJECT (actor), "my-post-map-id");
  if (map_id)
    {
      g_source_remove (GPOINTER_TO_UINT (map_id));
      g_object_set_data (G_OBJECT (actor), "my-post-map-id", NULL);
    }

  my_window_cancel_scheduled_sync (actor);
}

static gfloat
my_plugin_vertical_center (gfloat bar_height, gfloat item_height)
{
  return (bar_height - item_height) / 2.0f;
}

static void
my_plugin_make_click_target (ClutterActor *actor,
                             gfloat        width,
                             gfloat        height)
{
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_set_size (actor, width, height);
}

static ClutterActor *
my_plugin_create_menu_bar_item (ClutterActor *ref_actor,
                                const char   *font_desc,
                                const char   *text,
                                gfloat        r,
                                gfloat        g,
                                gfloat        b)
{
  ClutterActor *bin;
  ClutterActor *label;
  gfloat label_w;
  gfloat label_h;

  label = my_plugin_create_text_label (ref_actor, font_desc, text, r, g, b);
  label_w = clutter_actor_get_width (label);
  label_h = clutter_actor_get_height (label);

  bin = clutter_actor_new ();
  my_plugin_make_click_target (bin, label_w, MENU_BAR_HIT_HEIGHT);
  clutter_actor_add_child (bin, label);
  clutter_actor_set_position (label,
                              0.0f,
                              my_plugin_vertical_center (MENU_BAR_HIT_HEIGHT, label_h));
  clutter_actor_show (label);

  return bin;
}

static void
my_plugin_clear_menu_bar_labels (MyPlugin *plugin)
{
  guint i;

  for (i = 0; i < plugin->n_menu_bar_labels; i++)
    {
      if (plugin->menu_bar_labels[i])
        {
          clutter_actor_destroy (plugin->menu_bar_labels[i]);
          plugin->menu_bar_labels[i] = NULL;
        }
    }
  plugin->n_menu_bar_labels = 0;
}

static void
my_plugin_rebuild_menu_bar (MyPlugin *plugin)
{
  const MyAquaPalette *palette;
  guint n;
  guint i;
  gboolean from_app;

  if (!plugin->panel)
    return;

  palette = my_theme_get_palette (NULL);
  from_app = plugin->global_menu &&
             my_global_menu_has_app_menu (plugin->global_menu);
  n = from_app ? my_global_menu_get_n_top (plugin->global_menu)
               : G_N_ELEMENTS (menu_bar_items);
  if (n > MY_GLOBAL_MENU_MAX_TOP)
    n = MY_GLOBAL_MENU_MAX_TOP;

  my_plugin_clear_menu_bar_labels (plugin);
  plugin->menu_bar_from_app = from_app;

  for (i = 0; i < n; i++)
    {
      const char *text;
      ClutterActor *label;

      if (from_app)
        {
          text = my_global_menu_get_top_label (plugin->global_menu, i);
          if (!text)
            text = "Menu";
        }
      else
        text = menu_bar_items[i];

      label = my_plugin_create_menu_bar_item (plugin->panel,
                                              OOZE_UI_FONT,
                                              text,
                                              (gfloat) palette->menu_text_r,
                                              (gfloat) palette->menu_text_g,
                                              (gfloat) palette->menu_text_b);
      g_signal_connect (label,
                        "button-press-event",
                        G_CALLBACK (on_menu_bar_pressed),
                        plugin);

      /* Keep labels before the clock: insert after ooze button. */
      if (plugin->clock_label)
        clutter_actor_insert_child_below (plugin->panel, label, plugin->clock_label);
      else
        clutter_actor_add_child (plugin->panel, label);

      plugin->menu_bar_labels[i] = label;
    }

  plugin->n_menu_bar_labels = n;
  my_plugin_layout_menu_labels (plugin);
}

static void
my_plugin_on_global_menu_changed (gpointer user_data)
{
  MyPlugin *plugin = MY_PLUGIN (user_data);

  if (plugin->menu_popup && my_aqua_menu_is_open (plugin->menu_popup))
    my_aqua_menu_close (plugin->menu_popup);

  my_plugin_rebuild_menu_bar (plugin);
}

static void
my_plugin_layout_menu_labels (MyPlugin *plugin)
{
  ClutterActor *child;
  gfloat menu_x;

  if (!plugin->panel)
    return;

  if (plugin->menu_icon)
    menu_x = OOZE_BUTTON_MARGIN + clutter_actor_get_width (plugin->menu_icon) + 6.0f;
  else
    menu_x = OOZE_BUTTON_MARGIN;

  for (child = clutter_actor_get_first_child (plugin->panel);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      if (my_plugin_panel_child_is_fixed_chrome (plugin, child))
        continue;

      clutter_actor_set_position (child,
                                  menu_x,
                                  0.0f);
      menu_x += clutter_actor_get_width (child) + MENU_ITEM_GAP;
    }
}

static void
my_plugin_refresh_ooze_button (MyPlugin *plugin)
{
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  if (!plugin->menu_icon)
    return;

  content = my_aqua_ooze_button_content (plugin->panel, &width, &height);
  if (!content)
    return;

  my_aqua_actor_set_content (plugin->menu_icon,
                             g_steal_pointer (&content),
                             width,
                             height);
  clutter_actor_set_size (plugin->menu_icon, (gfloat) width, (gfloat) height);
  clutter_actor_set_position (plugin->menu_icon,
                              OOZE_BUTTON_MARGIN,
                              my_plugin_vertical_center (PANEL_HEIGHT, (gfloat) height));
}

static void
my_plugin_raise_ssd_titlebar (MetaWindowActor *actor)
{
  MyWindowChrome *chrome;
  ClutterActor *window_actor;
  ClutterActor *window_group;

  chrome = g_object_get_data (G_OBJECT (actor), "my-window-chrome");
  if (!chrome || !CLUTTER_IS_ACTOR (chrome->titlebar))
    return;

  window_actor = CLUTTER_ACTOR (actor);
  window_group = clutter_actor_get_parent (window_actor);
  if (!window_group)
    return;

  clutter_actor_set_child_above_sibling (window_group,
                                         chrome->titlebar,
                                         NULL);
}

static gboolean
my_plugin_sync_window_chrome_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  g_object_set_data (G_OBJECT (actor), "my-chrome-sync-id", NULL);
  my_plugin_sync_window_chrome (actor);
  return G_SOURCE_REMOVE;
}

static void
my_plugin_schedule_window_chrome_sync (MetaWindowActor *actor)
{
  guint id;

  if (g_object_get_data (G_OBJECT (actor), "my-chrome-sync-id"))
    return;

  g_object_ref (actor);
  id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                        my_plugin_sync_window_chrome_idle,
                        actor,
                        g_object_unref);
  g_object_set_data (G_OBJECT (actor),
                     "my-chrome-sync-id",
                     GUINT_TO_POINTER (id));
}

static void
my_plugin_sync_window_chrome (MetaWindowActor *actor)
{
  MyWindowChrome *chrome;
  MetaWindow *window;
  MtkRectangle frame;
  g_autoptr (ClutterContent) titlebar_content = NULL;
  const MyAquaPalette *palette;

  chrome = g_object_get_data (G_OBJECT (actor), "my-window-chrome");
  palette = my_theme_get_palette (NULL);

  if (meta_window_actor_is_destroyed (actor))
    return;

  window = meta_window_actor_get_meta_window (actor);
  if (!window)
    return;

  meta_window_get_frame_rect (window, &frame);

  if (frame.width <= 0 || frame.height <= 0)
    return;

  if (chrome && CLUTTER_IS_ACTOR (chrome->titlebar))
    {
      gboolean minimized;

      minimized = my_plugin_window_is_minimized (window);
      clutter_actor_set_position (chrome->titlebar,
                                  (gfloat) frame.x,
                                  (gfloat) frame.y);
      clutter_actor_set_size (chrome->titlebar,
                              (gfloat) frame.width,
                              AQUA_TITLEBAR_HEIGHT);

      if (frame.width != chrome->last_titlebar_width)
        {
          titlebar_content = my_aqua_pinstripe_content (chrome->titlebar,
                                                        frame.width,
                                                        (int) AQUA_TITLEBAR_HEIGHT);
          if (titlebar_content)
            my_aqua_actor_set_content (chrome->titlebar,
                                       g_steal_pointer (&titlebar_content),
                                       frame.width,
                                       (int) AQUA_TITLEBAR_HEIGHT);
          chrome->last_titlebar_width = frame.width;
        }

      /* Compositor titlebar is a sibling of the window actor — hide it with
       * the window or minimize looks broken (titlebar left floating). */
      if (minimized)
        clutter_actor_hide (chrome->titlebar);
      else
        clutter_actor_show (chrome->titlebar);
    }

  if (chrome && CLUTTER_IS_ACTOR (chrome->title_label))
    {
      gfloat label_width;
      const char *title = meta_window_get_title (window);

      if (!chrome->last_title || g_strcmp0 (chrome->last_title, title) != 0)
        {
          g_free (chrome->last_title);
          chrome->last_title = g_strdup (title);
          my_plugin_set_text_label (chrome->title_label,
                                    OOZE_UI_FONT,
                                    title,
                                    (gfloat) palette->title_text_r,
                                    (gfloat) palette->title_text_g,
                                    (gfloat) palette->title_text_b);
        }

      label_width = clutter_actor_get_width (chrome->title_label);
      clutter_actor_set_position (chrome->title_label,
                                  ((gfloat) frame.width - label_width) / 2.0f,
                                  my_plugin_vertical_center (AQUA_TITLEBAR_HEIGHT,
                                                             clutter_actor_get_height (chrome->title_label)));
    }

  my_window_sync (actor);
}

static gboolean
on_titlebar_button_press (ClutterActor *actor G_GNUC_UNUSED,
                          ClutterEvent *event,
                          MetaWindow   *window)
{
  if (my_window_is_client_decorated (window))
    return CLUTTER_EVENT_PROPAGATE;

  if (!meta_window_allows_move (window))
    return CLUTTER_EVENT_PROPAGATE;

  if (my_window_begin_grab_from_event (window,
                                       event,
                                       META_GRAB_OP_MOVING))
    return CLUTTER_EVENT_STOP;

  return CLUTTER_EVENT_PROPAGATE;
}

static void
my_plugin_remove_window_chrome (MetaWindowActor *actor)
{
  MyWindowChrome *chrome;
  MetaWindow *window;

  chrome = g_object_get_data (G_OBJECT (actor), "my-window-chrome");
  if (!chrome)
    return;

  my_plugin_cancel_window_idle_ops (actor);

  /* Disconnect position-changed before the chrome struct is freed. */
  if (chrome->position_changed_id)
    {
      window = meta_window_actor_get_meta_window (actor);
      if (window)
        g_signal_handler_disconnect (window, chrome->position_changed_id);
      chrome->position_changed_id = 0;
    }

  /* NULL-assign calls my_window_chrome_free (destroy notify). */
  g_object_set_data (G_OBJECT (actor), "my-window-chrome", NULL);
}

static void
my_window_chrome_free (gpointer data)
{
  MyWindowChrome *chrome = data;

  /* title_label is a child of titlebar; destroying titlebar destroys it too. */
  chrome->title_label = NULL;
  g_clear_pointer (&chrome->titlebar, clutter_actor_destroy);
  g_clear_pointer (&chrome->last_title, g_free);
  g_free (chrome);
}

static gboolean
on_traffic_close_pressed (ClutterActor *actor G_GNUC_UNUSED,
                          ClutterEvent *event,
                          MetaWindow   *window)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  meta_window_delete (window, clutter_event_get_time (event));
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_traffic_minimize_pressed (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             MetaWindow   *window)
{
  MetaWindowActor *window_actor;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  if (!meta_window_can_minimize (window))
    {
      g_warning ("Ooze: window \"%s\" cannot minimize",
                 meta_window_get_title (window));
      return CLUTTER_EVENT_STOP;
    }

  /* Hide compositor chrome immediately so it doesn't linger mid-animation. */
  window_actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
  if (window_actor)
    my_plugin_set_chrome_visible (window_actor, FALSE);

  meta_window_minimize (window);
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_traffic_zoom_pressed (ClutterActor *actor G_GNUC_UNUSED,
                         ClutterEvent *event,
                         MetaWindow   *window)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  if (meta_window_is_maximized (window))
    meta_window_unmaximize (window);
  else
    meta_window_maximize (window);

  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
my_plugin_create_traffic_light (ClutterActor *ref,
                                MetaWindow   *window,
                                gfloat        r,
                                gfloat        g,
                                gfloat        b,
                                GCallback     handler)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;

  content = my_aqua_traffic_light_content (ref,
                                           AQUA_TRAFFIC_LIGHT_SIZE,
                                           r, g, b);
  actor = clutter_actor_new ();
  clutter_actor_set_size (actor, AQUA_TRAFFIC_LIGHT_SIZE, AQUA_TRAFFIC_LIGHT_SIZE);
  clutter_actor_set_reactive (actor, TRUE);
  if (content)
    my_aqua_actor_set_content (actor,
                               g_steal_pointer (&content),
                               AQUA_TRAFFIC_LIGHT_SIZE,
                               AQUA_TRAFFIC_LIGHT_SIZE);

  g_signal_connect (actor,
                    "button-press-event",
                    handler,
                    window);

  return actor;
}

static void
my_plugin_apply_window_chrome (MyPlugin         *plugin,
                               MetaWindowActor  *actor)
{
  MetaWindow *window;
  MetaDisplay *display;
  MetaCompositor *compositor;
  ClutterActor *window_group;
  ClutterActor *window_actor;
  MyWindowChrome *chrome;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);

  if (g_object_get_data (G_OBJECT (actor), "my-window-chrome"))
    return;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return;

  /*
   * Ooze apps (org.ooze.*) draw their own GTK window frame via Ooze Gel,
   * so we must not add a second compositor-level titlebar on top.
   * All other apps – including CSD Wayland clients like Inkscape – get
   * the Aqua traffic-light overlay drawn by the compositor.
   * Resize grab overlays are handled separately in my_window_setup, which
   * already skips CSD windows to avoid crashing mutter.
   */
  if (my_window_uses_ooze_client_chrome (window))
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  compositor = meta_display_get_compositor (display);
  window_group = meta_compositor_get_window_group (compositor);

  chrome = g_new0 (MyWindowChrome, 1);

  {
    ClutterActor *close_btn;
    ClutterActor *minimize_btn;
    ClutterActor *zoom_btn;
    gfloat x;

    chrome->titlebar = clutter_actor_new ();
    clutter_actor_set_reactive (chrome->titlebar, TRUE);
    clutter_actor_hide (chrome->titlebar);

    close_btn = my_plugin_create_traffic_light (window_actor,
                                                window,
                                                AQUA_TRAFFIC_CLOSE_R,
                                                AQUA_TRAFFIC_CLOSE_G,
                                                AQUA_TRAFFIC_CLOSE_B,
                                                G_CALLBACK (on_traffic_close_pressed));
    minimize_btn = my_plugin_create_traffic_light (window_actor,
                                                   window,
                                                   AQUA_TRAFFIC_MINIMIZE_R,
                                                   AQUA_TRAFFIC_MINIMIZE_G,
                                                   AQUA_TRAFFIC_MINIMIZE_B,
                                                   G_CALLBACK (on_traffic_minimize_pressed));
    zoom_btn = my_plugin_create_traffic_light (window_actor,
                                               window,
                                               AQUA_TRAFFIC_ZOOM_R,
                                               AQUA_TRAFFIC_ZOOM_G,
                                               AQUA_TRAFFIC_ZOOM_B,
                                               G_CALLBACK (on_traffic_zoom_pressed));

    x = AQUA_TRAFFIC_LIGHT_MARGIN;
    clutter_actor_add_child (chrome->titlebar, close_btn);
    clutter_actor_set_position (close_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - AQUA_TRAFFIC_LIGHT_SIZE) / 2.0f);
    x += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;

    clutter_actor_add_child (chrome->titlebar, minimize_btn);
    clutter_actor_set_position (minimize_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - AQUA_TRAFFIC_LIGHT_SIZE) / 2.0f);
    x += AQUA_TRAFFIC_LIGHT_SIZE + AQUA_TRAFFIC_LIGHT_GAP;

    clutter_actor_add_child (chrome->titlebar, zoom_btn);
    clutter_actor_set_position (zoom_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - AQUA_TRAFFIC_LIGHT_SIZE) / 2.0f);

    chrome->title_label = my_plugin_create_text_label (window_actor,
                                                       OOZE_UI_FONT,
                                                       meta_window_get_title (window),
                                                       (gfloat) my_theme_get_palette (NULL)->title_text_r,
                                                       (gfloat) my_theme_get_palette (NULL)->title_text_g,
                                                       (gfloat) my_theme_get_palette (NULL)->title_text_b);
    chrome->last_title = g_strdup (meta_window_get_title (window));
    clutter_actor_add_child (chrome->titlebar, chrome->title_label);
    clutter_actor_set_width (chrome->title_label, 240.0f);

    g_signal_connect (chrome->titlebar,
                      "button-press-event",
                      G_CALLBACK (on_titlebar_button_press),
                      window);

    clutter_actor_add_child (window_group, chrome->titlebar);
    clutter_actor_set_child_above_sibling (window_group,
                                           chrome->titlebar,
                                           window_actor);
  }

  g_object_set_data_full (G_OBJECT (actor),
                          "my-window-chrome",
                          chrome,
                          my_window_chrome_free);

  /* Reposition titlebar whenever the window moves. */
  chrome->position_changed_id =
    g_signal_connect_swapped (window,
                              "position-changed",
                              G_CALLBACK (my_plugin_schedule_window_chrome_sync),
                              actor);
}

static gboolean
my_plugin_post_map_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  /* Clear id first so cancel_idle_ops called from within setup does not
   * try to remove an already-running source. */
  g_object_set_data (G_OBJECT (actor), "my-post-map-id", NULL);

  my_window_setup (actor);

  if (g_object_get_data (G_OBJECT (actor), "my-window-chrome"))
    {
      my_plugin_raise_ssd_titlebar (actor);
      my_plugin_schedule_window_chrome_sync (actor);
    }
  else
    {
      my_window_schedule_sync (actor);
    }

  return G_SOURCE_REMOVE;
  /* g_object_unref via GDestroyNotify registered in schedule_post_map */
}

static void
my_plugin_schedule_post_map (MetaWindowActor *actor)
{
  guint id;

  if (g_object_get_data (G_OBJECT (actor), "my-post-map-id"))
    return;

  g_object_ref (actor);
  id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                        my_plugin_post_map_idle,
                        actor,
                        g_object_unref);   /* released whether idle fires or is cancelled */
  g_object_set_data (G_OBJECT (actor),
                     "my-post-map-id",
                     GUINT_TO_POINTER (id));
}

static gsize
my_plugin_count_dock_icons (MyPlugin *plugin)
{
  ClutterActor *child;
  gsize count = 0;

  if (!plugin->aqua_dock_icons)
    return 0;

  for (child = clutter_actor_get_first_child (plugin->aqua_dock_icons);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    count++;

  return count;
}

static void
my_plugin_update_aqua_dock_layout (MyPlugin    *plugin,
                                   MetaDisplay *display)
{
  int width;
  int height;
  gsize icon_count;
  gfloat plate_width;
  gfloat dock_height;
  gfloat x;

  if (!plugin->aqua_dock)
    return;

  meta_display_get_size (display, &width, &height);
  icon_count = my_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    icon_count = 4;

  plate_width = (gfloat) icon_count * AQUA_DOCK_ITEM_SIZE +
                ((gfloat) icon_count - 1.0f) * AQUA_DOCK_ITEM_GAP +
                2.0f * AQUA_DOCK_PADDING;
  dock_height = AQUA_DOCK_PLATE_H + AQUA_DOCK_BOTTOM_GAP;

  x = ((gfloat) width - plate_width) / 2.0f;

  my_plugin_refresh_dock_plate (plugin, (int) plate_width);

  clutter_actor_set_size (plugin->aqua_dock, plate_width, dock_height);
  clutter_actor_set_position (plugin->aqua_dock,
                              x,
                              (gfloat) height - dock_height);

  if (plugin->aqua_dock_plate)
    {
      clutter_actor_set_size (plugin->aqua_dock_plate,
                              plate_width,
                              AQUA_DOCK_PLATE_H);
      clutter_actor_set_position (plugin->aqua_dock_plate, 0.0f, 0.0f);
    }

  if (plugin->aqua_dock_icons)
    {
      gfloat icons_width;
      gfloat icons_y;

      icons_width = plate_width - 2.0f * AQUA_DOCK_PADDING;
      icons_y = (AQUA_DOCK_PLATE_H - AQUA_DOCK_ITEM_SIZE) / 2.0f;

      clutter_actor_set_size (plugin->aqua_dock_icons,
                              icons_width,
                              AQUA_DOCK_ITEM_SIZE);
      clutter_actor_set_position (plugin->aqua_dock_icons,
                                  AQUA_DOCK_PADDING,
                                  icons_y);
    }
}

static void
my_plugin_update_clock (MyPlugin *plugin)
{
  time_t now;
  struct tm *tm_local;
  char buffer[64];

  if (!plugin->clock_label)
    return;

  now = time (NULL);
  tm_local = localtime (&now);
  if (!tm_local)
    return;

  strftime (buffer, sizeof buffer, "%a %b %e  %l:%M %p", tm_local);
  {
    const MyAquaPalette *palette = my_theme_get_palette (NULL);

    my_plugin_set_text_label (plugin->clock_label,
                              OOZE_UI_FONT,
                              buffer,
                              (gfloat) palette->menu_text_r,
                              (gfloat) palette->menu_text_g,
                              (gfloat) palette->menu_text_b);
  }
}

static gboolean
on_clock_tick (gpointer user_data)
{
  my_plugin_update_clock (MY_PLUGIN (user_data));
  return G_SOURCE_CONTINUE;
}

static void
my_plugin_raise_chrome (MyPlugin    *plugin,
                        MetaDisplay *display)
{
  MetaCompositor *compositor;
  ClutterActor *stage;
  ClutterActor *window_group;
  ClutterActor *above;

  if (!plugin->panel && !plugin->aqua_dock)
    return;

  compositor = meta_display_get_compositor (display);
  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  window_group = meta_compositor_get_window_group (compositor);
  above = window_group;

  if (plugin->aqua_dock)
    {
      clutter_actor_set_child_above_sibling (stage, plugin->aqua_dock, above);
      above = plugin->aqua_dock;
    }

  if (plugin->panel)
    clutter_actor_set_child_above_sibling (stage, plugin->panel, above);

  if (plugin->menu_popup && my_aqua_menu_is_open (plugin->menu_popup))
    my_aqua_menu_raise (plugin->menu_popup);
}

static void
my_plugin_update_builtin_struts (MyPlugin    *plugin G_GNUC_UNUSED,
                                 MetaDisplay *display)
{
  MetaWorkspaceManager *wm;
  GList *workspaces;
  GList *l;
  int width;
  int height;
  int panel_h;
  int dock_h;

  if (!display)
    return;

  meta_display_get_size (display, &width, &height);
  if (width < 1 || height < 1)
    return;

  panel_h = (int) PANEL_HEIGHT;
  dock_h = (int) (AQUA_DOCK_PLATE_H + AQUA_DOCK_BOTTOM_GAP);
  if (panel_h + dock_h >= height)
    return;

  wm = meta_display_get_workspace_manager (display);
  if (!wm)
    return;

  workspaces = meta_workspace_manager_get_workspaces (wm);
  for (l = workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      GSList *struts = NULL;
      MetaStrut *top;
      MetaStrut *bottom;

      top = g_new0 (MetaStrut, 1);
      top->side = META_SIDE_TOP;
      top->rect.x = 0;
      top->rect.y = 0;
      top->rect.width = width;
      top->rect.height = panel_h;
      struts = g_slist_prepend (struts, top);

      bottom = g_new0 (MetaStrut, 1);
      bottom->side = META_SIDE_BOTTOM;
      bottom->rect.x = 0;
      bottom->rect.y = height - dock_h;
      bottom->rect.width = width;
      bottom->rect.height = dock_h;
      struts = g_slist_prepend (struts, bottom);

      meta_workspace_set_builtin_struts (workspace, struts);
      g_slist_free_full (struts, g_free);
    }
}

static void
my_plugin_update_layout (MyPlugin     *plugin,
                         MetaDisplay  *display)
{
  int width;
  int height;
  gfloat clock_width;

  meta_display_get_size (display, &width, &height);

  if (plugin->panel)
    {
      my_plugin_refresh_panel_texture (plugin, width);
      clutter_actor_set_size (plugin->panel, (gfloat) width, PANEL_HEIGHT);
      clutter_actor_set_position (plugin->panel, 0.0f, 0.0f);
    }

  my_plugin_raise_chrome (plugin, display);

  if (plugin->clock_label)
    {
      clock_width = clutter_actor_get_width (plugin->clock_label);
      clutter_actor_set_position (plugin->clock_label,
                                  (gfloat) width - clock_width - 12.0f,
                                  my_plugin_vertical_center (PANEL_HEIGHT,
                                                             clutter_actor_get_height (plugin->clock_label)));
    }

  my_plugin_layout_menu_labels (plugin);

  my_plugin_update_aqua_dock_layout (plugin, display);
  my_plugin_update_builtin_struts (plugin, display);
}

static void
my_plugin_setup_panel (MyPlugin       *plugin,
                       MetaDisplay    *display,
                       MetaCompositor *compositor)
{
  ClutterActor *stage;
  g_autoptr (ClutterContent) ooze_content = NULL;
  int ooze_width = 1;
  int ooze_height = 1;
  const MyAquaPalette *palette;

  if (plugin->panel)
    return;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  palette = my_theme_get_palette (NULL);

  plugin->panel = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->panel, TRUE);

  ooze_content = my_aqua_ooze_button_content (plugin->panel,
                                              &ooze_width,
                                              &ooze_height);
  plugin->menu_icon = clutter_actor_new ();
  my_plugin_make_click_target (plugin->menu_icon,
                               (gfloat) ooze_width,
                               PANEL_HEIGHT);
  if (ooze_content)
    my_aqua_actor_set_content (plugin->menu_icon,
                               g_steal_pointer (&ooze_content),
                               ooze_width,
                               ooze_height);
  clutter_actor_set_size (plugin->menu_icon,
                          (gfloat) ooze_width,
                          (gfloat) ooze_height);
  clutter_actor_set_position (plugin->menu_icon,
                              OOZE_BUTTON_MARGIN,
                              0.0f);
  clutter_actor_add_child (plugin->panel, plugin->menu_icon);

  if (!plugin->menu_popup)
    {
      plugin->menu_popup = my_aqua_menu_new (plugin->context,
                                             stage,
                                             my_plugin_menu_action,
                                             plugin);
    }

  g_signal_connect (plugin->menu_icon,
                    "button-press-event",
                    G_CALLBACK (on_ooze_button_pressed),
                    plugin);

  plugin->clock_label = my_plugin_create_text_label (plugin->panel,
                                                     OOZE_UI_FONT,
                                                     "Sat Jan  1  12:00 PM",
                                                     (gfloat) palette->menu_text_r,
                                                     (gfloat) palette->menu_text_g,
                                                     (gfloat) palette->menu_text_b);
  clutter_actor_add_child (plugin->panel, plugin->clock_label);

  my_plugin_rebuild_menu_bar (plugin);
  my_plugin_update_clock (plugin);

  if (!plugin->clock_timer)
    plugin->clock_timer = g_timeout_add_seconds (30, on_clock_tick, plugin);

  clutter_actor_add_child (stage, plugin->panel);
  clutter_actor_show (plugin->panel);

  my_plugin_update_layout (plugin, display);
}

static gboolean
on_spot_placeholder_pressed (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             MyPlugin    *plugin)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  my_dock_launch_spot (plugin->context);
  return CLUTTER_EVENT_STOP;
}

static void
my_plugin_add_dock_placeholders (MyPlugin     *plugin,
                                 MetaDisplay  *display,
                                 ClutterActor *stage)
{
  static const struct
  {
    gfloat r;
    gfloat g;
    gfloat b;
  } colors[] = {
    { 0.22f, 0.48f, 0.92f },
    { 0.92f, 0.28f, 0.28f },
    { 0.28f, 0.78f, 0.38f },
    { 0.98f, 0.68f, 0.18f },
  };
  gsize i;
  int logical = (int) AQUA_DOCK_ITEM_SIZE;
  int texture = my_aqua_icon_texture_size (display, logical);

  for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
      ClutterActor *item;
      g_autoptr (ClutterContent) content = NULL;

      if (i == 0)
        content = my_aqua_spot_icon_content (stage, display, logical);
      else
        content = my_aqua_dock_icon_content (stage,
                                             texture,
                                             colors[i].r,
                                             colors[i].g,
                                             colors[i].b);
      item = clutter_actor_new ();
      if (content)
        my_aqua_actor_set_scaled_content (item,
                                          g_steal_pointer (&content),
                                          logical,
                                          logical,
                                          texture,
                                          texture);

      if (i == 0)
        {
          clutter_actor_set_reactive (item, TRUE);
          g_signal_connect (item,
                            "button-press-event",
                            G_CALLBACK (on_spot_placeholder_pressed),
                            plugin);
        }

      clutter_actor_add_child (plugin->aqua_dock_icons, item);
    }
}

static void
my_plugin_setup_aqua_dock (MyPlugin       *plugin,
                           MetaDisplay    *display,
                           MetaCompositor *compositor)
{
  ClutterActor *stage;
  ClutterActor *window_group;
  ClutterLayoutManager *layout;
  gsize icon_count;

  if (plugin->aqua_dock)
    return;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  window_group = meta_compositor_get_window_group (compositor);

  plugin->aqua_dock = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->aqua_dock, FALSE);

  plugin->aqua_dock_plate = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->aqua_dock_plate, FALSE);
  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_plate);

  plugin->aqua_dock_icons = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->aqua_dock_icons, FALSE);

  layout = clutter_box_layout_new ();
  clutter_box_layout_set_orientation (CLUTTER_BOX_LAYOUT (layout),
                                      CLUTTER_ORIENTATION_HORIZONTAL);
  clutter_box_layout_set_spacing (CLUTTER_BOX_LAYOUT (layout),
                                  AQUA_DOCK_ITEM_GAP);
  clutter_box_layout_set_homogeneous (CLUTTER_BOX_LAYOUT (layout), FALSE);
  clutter_actor_set_layout_manager (plugin->aqua_dock_icons, layout);

  if (plugin->context)
    {
      my_dock_populate_container (plugin->context,
                                  display,
                                  stage,
                                  plugin->aqua_dock_icons);
    }

  icon_count = my_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    my_plugin_add_dock_placeholders (plugin, display, stage);

  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_icons);

  clutter_actor_add_child (stage, plugin->aqua_dock);
  clutter_actor_set_child_above_sibling (stage, plugin->aqua_dock, window_group);
  clutter_actor_show (plugin->aqua_dock);

  my_plugin_update_aqua_dock_layout (plugin, display);
}

static void
my_plugin_on_monitors_changed (MetaMonitorManager *monitor_manager G_GNUC_UNUSED,
                               MyPlugin           *plugin)
{
  MetaDisplay *display;
  MetaCompositor *compositor;
  int n_monitors;
  int i;

  if (!plugin->background_group)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  compositor = meta_display_get_compositor (display);
  n_monitors = meta_display_get_n_monitors (display);

  if (n_monitors == 0)
    return;

  clutter_actor_destroy_all_children (plugin->background_group);

  for (i = 0; i < n_monitors; i++)
    {
      MtkRectangle rect;
      ClutterActor *background_actor;
      g_autoptr (ClutterContent) wallpaper = NULL;

      meta_display_get_monitor_geometry (display, i, &rect);

      background_actor = clutter_actor_new ();
      wallpaper = my_aqua_wallpaper_content (background_actor,
                                             rect.width,
                                             rect.height);
      if (wallpaper)
        my_aqua_actor_set_content (background_actor,
                                   g_steal_pointer (&wallpaper),
                                   rect.width,
                                   rect.height);
      clutter_actor_set_position (background_actor,
                                  (gfloat) rect.x,
                                  (gfloat) rect.y);
      clutter_actor_set_size (background_actor,
                              (gfloat) rect.width,
                              (gfloat) rect.height);
      clutter_actor_add_child (plugin->background_group, background_actor);
      clutter_actor_show (background_actor);

      {
        ClutterActor *desktop_icons;

        desktop_icons = my_desktop_icons_create (plugin->context,
                                                 display,
                                                 background_actor,
                                                 i,
                                                 rect.width,
                                                 rect.height);
        clutter_actor_add_child (background_actor, desktop_icons);
        clutter_actor_show (desktop_icons);
      }
    }

  clutter_actor_show (plugin->background_group);

  my_plugin_setup_panel (plugin, display, compositor);
  my_plugin_setup_aqua_dock (plugin, display, compositor);
  my_plugin_update_layout (plugin, display);
}

static void
my_plugin_on_workspace_added (MetaWorkspaceManager *wm G_GNUC_UNUSED,
                              gint                  index G_GNUC_UNUSED,
                              MyPlugin             *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (plugin));
  my_plugin_update_builtin_struts (plugin, display);
}

static void
my_plugin_init_ui (MyPlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaCompositor *compositor;
  MetaMonitorManager *monitor_manager;
  MetaContext *context;

  if (plugin->background_group)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  context = meta_display_get_context (display);
  backend = meta_context_get_backend (context);
  compositor = meta_display_get_compositor (display);
  monitor_manager = meta_backend_get_monitor_manager (backend);

  plugin->context = g_object_ref (context);
  plugin->monitor_manager = monitor_manager;
  plugin->background_group = meta_background_group_new ();
  clutter_actor_insert_child_below (meta_compositor_get_window_group (compositor),
                                    plugin->background_group,
                                    NULL);

  plugin->monitors_changed_handler =
    g_signal_connect (monitor_manager,
                      "monitors-changed",
                      G_CALLBACK (my_plugin_on_monitors_changed),
                      plugin);
  my_plugin_on_monitors_changed (monitor_manager, plugin);

  {
    MetaWorkspaceManager *wm = meta_display_get_workspace_manager (display);

    if (wm && !plugin->workspace_added_handler)
      {
        plugin->workspace_added_handler =
          g_signal_connect (wm,
                            "workspace-added",
                            G_CALLBACK (my_plugin_on_workspace_added),
                            plugin);
      }
  }

  my_theme_get_default ();
  my_theme_watch (NULL, my_plugin_on_theme_changed, plugin);

  if (!plugin->global_menu)
    {
      plugin->global_menu = my_global_menu_new (display);
      my_global_menu_set_changed_callback (plugin->global_menu,
                                           my_plugin_on_global_menu_changed,
                                           plugin);
      my_plugin_rebuild_menu_bar (plugin);
    }
}

static void
my_plugin_set_chrome_visible (MetaWindowActor *actor, gboolean visible)
{
  MyWindowChrome *chrome;

  chrome = g_object_get_data (G_OBJECT (actor), "my-window-chrome");
  if (!chrome || !CLUTTER_IS_ACTOR (chrome->titlebar))
    return;

  if (visible)
    clutter_actor_show (chrome->titlebar);
  else
    clutter_actor_hide (chrome->titlebar);
}

static gboolean
my_plugin_window_is_minimized (MetaWindow *window)
{
  gboolean minimized = FALSE;

  if (!window)
    return FALSE;
  g_object_get (window, "minimized", &minimized, NULL);
  return minimized;
}

static void
my_plugin_get_lamp_icon_rect (MetaPlugin      *plugin,
                              MetaWindowActor *window_actor,
                              gfloat          *out_x,
                              gfloat          *out_y,
                              gfloat          *out_w,
                              gfloat          *out_h)
{
  MetaWindow *window;
  MtkRectangle icon;
  MetaDisplay *display;
  int screen_w = 1280, screen_h = 800;
  MyPlugin *self = MY_PLUGIN (plugin);

  window = meta_window_actor_get_meta_window (window_actor);
  display = meta_plugin_get_display (plugin);
  if (display)
    meta_display_get_size (display, &screen_w, &screen_h);

  if (window && meta_window_get_icon_geometry (window, &icon) &&
      icon.width > 0 && icon.height > 0)
    {
      *out_x = (gfloat) icon.x;
      *out_y = (gfloat) icon.y;
      *out_w = (gfloat) icon.width;
      *out_h = (gfloat) icon.height;
      return;
    }

  if (self->aqua_dock)
    {
      *out_x = clutter_actor_get_x (self->aqua_dock);
      *out_y = clutter_actor_get_y (self->aqua_dock);
      *out_w = clutter_actor_get_width (self->aqua_dock);
      *out_h = clutter_actor_get_height (self->aqua_dock);
      return;
    }

  *out_x = (gfloat) screen_w / 2.0f - 24.f;
  *out_y = (gfloat) screen_h - AQUA_DOCK_PLATE_H;
  *out_w = 48.f;
  *out_h = AQUA_DOCK_PLATE_H;
}

static void
my_plugin_magic_lamp_done (MetaPlugin      *plugin,
                           MetaWindowActor *actor,
                           gboolean         unminimize,
                           gpointer         user_data G_GNUC_UNUSED)
{
  if (!actor || meta_window_actor_is_destroyed (actor))
    return;

  if (unminimize)
    {
      my_plugin_set_chrome_visible (actor, TRUE);
      meta_plugin_unminimize_completed (plugin, actor);
    }
  else
    {
      my_plugin_set_chrome_visible (actor, FALSE);
      meta_plugin_minimize_completed (plugin, actor);
    }
}

static void
my_plugin_run_magic_lamp (MetaPlugin      *plugin,
                          MetaWindowActor *window_actor,
                          gboolean         unminimize)
{
  gfloat icon_x, icon_y, icon_w, icon_h;

  if (meta_window_actor_is_destroyed (window_actor))
    {
      if (unminimize)
        meta_plugin_unminimize_completed (plugin, window_actor);
      else
        meta_plugin_minimize_completed (plugin, window_actor);
      return;
    }

  my_plugin_set_chrome_visible (window_actor, FALSE);
  my_plugin_get_lamp_icon_rect (plugin, window_actor,
                                &icon_x, &icon_y, &icon_w, &icon_h);
  my_magic_lamp_run (plugin, window_actor, unminimize,
                     icon_x, icon_y, icon_w, icon_h,
                     my_plugin_magic_lamp_done, NULL);
}

static void
my_plugin_minimize (MetaPlugin      *plugin,
                    MetaWindowActor *actor)
{
  MetaWindow *window = meta_window_actor_get_meta_window (actor);

  if (!window || meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    {
      meta_plugin_minimize_completed (plugin, actor);
      return;
    }

  my_plugin_run_magic_lamp (plugin, actor, FALSE);
}

static void
my_plugin_unminimize (MetaPlugin      *plugin,
                      MetaWindowActor *actor)
{
  MetaWindow *window = meta_window_actor_get_meta_window (actor);

  if (!window || meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    {
      meta_plugin_unminimize_completed (plugin, actor);
      return;
    }

  my_plugin_run_magic_lamp (plugin, actor, TRUE);
}

static void
my_plugin_map (MetaPlugin      *plugin,
               MetaWindowActor *actor)
{
  MetaWindow *window;
  ClutterActor *window_actor;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);

  clutter_actor_show (window_actor);

  my_plugin_apply_window_chrome (MY_PLUGIN (plugin), actor);

  meta_window_focus (window, clutter_get_current_event_time ());
  meta_plugin_map_completed (plugin, actor);
  my_plugin_schedule_post_map (actor);
}

static void
my_plugin_destroy (MetaPlugin      *plugin,
                   MetaWindowActor *actor)
{
  my_plugin_cancel_window_idle_ops (actor);
  my_plugin_remove_window_chrome (actor);
  my_window_teardown (actor);
  meta_plugin_destroy_completed (plugin, actor);
}

static void
my_plugin_size_change (MetaPlugin      *plugin G_GNUC_UNUSED,
                       MetaWindowActor *actor,
                       MetaSizeChange   which_change G_GNUC_UNUSED,
                       MtkRectangle    *old_frame_rect G_GNUC_UNUSED,
                       MtkRectangle    *old_buffer_rect G_GNUC_UNUSED)
{
  my_plugin_schedule_window_chrome_sync (actor);
  my_window_schedule_sync (actor);
  meta_plugin_size_change_completed (plugin, actor);
}

static gboolean
on_stage_key_press (ClutterActor *stage G_GNUC_UNUSED,
                    ClutterEvent *event,
                    MyPlugin    *plugin)
{
  ClutterModifierType state;

  if (plugin->menu_popup &&
      my_aqua_menu_handle_key (plugin->menu_popup, event))
    return CLUTTER_EVENT_STOP;

  state = clutter_event_get_state (event);
  if ((state & CLUTTER_SUPER_MASK) &&
      clutter_event_get_key_symbol (event) == CLUTTER_KEY_e)
    {
      my_dock_launch_spot (plugin->context);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
my_plugin_apply_wm_preferences (void)
{
  g_autoptr (GSettings) mutter = NULL;
  GSettingsSchemaSource *source;
  g_autoptr (GSettingsSchema) schema = NULL;

  source = g_settings_schema_source_get_default ();
  if (!source)
    return;

  schema = g_settings_schema_source_lookup (source, "org.gnome.mutter", TRUE);
  if (!schema)
    {
      g_warning ("MyPlugin: org.gnome.mutter schema missing; edge tiling unavailable");
      return;
    }

  mutter = g_settings_new_full (schema, NULL, NULL);

  /* Schema default is false; enable half-screen / maximize-on-edge snap. */
  if (g_settings_schema_has_key (schema, "edge-tiling"))
    g_settings_set_boolean (mutter, "edge-tiling", TRUE);

  /* Comfortable invisible border for SSD resize grabs (X11 / non-CSD). */
  if (g_settings_schema_has_key (schema, "draggable-border-width"))
    g_settings_set_int (mutter, "draggable-border-width", 12);

  g_settings_sync ();

  g_print ("MyPlugin: edge-tiling pref=%s settings=%s draggable-border-width=%d\n",
           meta_prefs_get_edge_tiling () ? "on" : "off",
           g_settings_get_boolean (mutter, "edge-tiling") ? "on" : "off",
           g_settings_schema_has_key (schema, "draggable-border-width")
             ? g_settings_get_int (mutter, "draggable-border-width") : -1);
}

static void
my_plugin_show_tile_preview (MetaPlugin   *plugin,
                             MetaWindow   *window G_GNUC_UNUSED,
                             MtkRectangle *tile_rect,
                             int           tile_monitor_number G_GNUC_UNUSED)
{
  MyPlugin *self = MY_PLUGIN (plugin);
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;
  CoglColor color;

  if (!tile_rect || tile_rect->width < 1 || tile_rect->height < 1)
    return;

  display = meta_plugin_get_display (plugin);
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

  if (!self->tile_preview)
    {
      self->tile_preview = clutter_actor_new ();
      clutter_actor_set_reactive (self->tile_preview, FALSE);
      cogl_color_init_from_4f (&color, 0.20f, 0.45f, 0.95f, 0.28f);
      clutter_actor_set_background_color (self->tile_preview, &color);
      clutter_actor_add_child (stage, self->tile_preview);
    }

  clutter_actor_set_position (self->tile_preview,
                              (gfloat) tile_rect->x,
                              (gfloat) tile_rect->y);
  clutter_actor_set_size (self->tile_preview,
                          (gfloat) tile_rect->width,
                          (gfloat) tile_rect->height);
  clutter_actor_set_child_above_sibling (stage, self->tile_preview, NULL);
  clutter_actor_show (self->tile_preview);
}

static void
my_plugin_hide_tile_preview (MetaPlugin *plugin)
{
  MyPlugin *self = MY_PLUGIN (plugin);

  if (self->tile_preview)
    clutter_actor_hide (self->tile_preview);
}

static void
my_plugin_start (MetaPlugin *plugin)
{
  MyPlugin *self = MY_PLUGIN (plugin);
  MetaDisplay *display;
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage;

  g_print ("MyPlugin: compositor plugin started successfully\n");

  my_plugin_apply_wm_preferences ();
  my_plugin_init_ui (self);

  display = meta_plugin_get_display (plugin);
  context = meta_display_get_context (display);
  backend = meta_context_get_backend (context);
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

  if (!self->stage_key_handler)
    {
      self->stage_key_handler =
        g_signal_connect (stage,
                          "key-press-event",
                          G_CALLBACK (on_stage_key_press),
                          self);
    }

  clutter_actor_show (stage);
}

static void
my_plugin_dispose (GObject *object)
{
  MyPlugin *plugin = MY_PLUGIN (object);

  if (plugin->clock_timer)
    {
      g_source_remove (plugin->clock_timer);
      plugin->clock_timer = 0;
    }

  if (plugin->monitors_changed_handler && plugin->monitor_manager)
    {
      g_signal_handler_disconnect (plugin->monitor_manager,
                                   plugin->monitors_changed_handler);
      plugin->monitors_changed_handler = 0;
    }

  if (plugin->workspace_added_handler)
    {
      MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (plugin));
      MetaWorkspaceManager *wm = display
        ? meta_display_get_workspace_manager (display) : NULL;

      if (wm)
        g_signal_handler_disconnect (wm, plugin->workspace_added_handler);
      plugin->workspace_added_handler = 0;
    }

  if (plugin->stage_key_handler)
    {
      MetaDisplay *display;
      MetaBackend *backend;
      ClutterActor *stage;

      display = meta_plugin_get_display (META_PLUGIN (plugin));
      backend = meta_context_get_backend (meta_display_get_context (display));
      stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
      g_signal_handler_disconnect (stage, plugin->stage_key_handler);
      plugin->stage_key_handler = 0;
    }

  my_theme_unwatch (NULL, my_plugin_on_theme_changed, plugin);

  my_aqua_menu_destroy (plugin->menu_popup);
  plugin->menu_popup = NULL;

  my_global_menu_free (plugin->global_menu);
  plugin->global_menu = NULL;

  g_clear_pointer (&plugin->panel, clutter_actor_destroy);
  plugin->menu_icon = NULL;
  plugin->clock_label = NULL;
  memset (plugin->menu_bar_labels, 0, sizeof plugin->menu_bar_labels);
  plugin->n_menu_bar_labels = 0;
  plugin->menu_bar_from_app = FALSE;
  g_clear_pointer (&plugin->aqua_dock, clutter_actor_destroy);
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  g_clear_pointer (&plugin->tile_preview, clutter_actor_destroy);
  g_clear_pointer (&plugin->background_group, clutter_actor_destroy);
  g_clear_object (&plugin->context);

  G_OBJECT_CLASS (my_plugin_parent_class)->dispose (object);
}

static void
my_plugin_class_init (MyPluginClass *klass)
{
  MetaPluginClass *plugin_class = META_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = my_plugin_dispose;

  plugin_class->start = my_plugin_start;
  plugin_class->map = my_plugin_map;
  plugin_class->destroy = my_plugin_destroy;
  plugin_class->size_change = my_plugin_size_change;
  plugin_class->minimize = my_plugin_minimize;
  plugin_class->unminimize = my_plugin_unminimize;
  plugin_class->show_tile_preview = my_plugin_show_tile_preview;
  plugin_class->hide_tile_preview = my_plugin_hide_tile_preview;
}

static void
my_plugin_init (MyPlugin *plugin)
{
  plugin->background_group = NULL;
  plugin->panel = NULL;
  plugin->menu_icon = NULL;
  plugin->clock_label = NULL;
  memset (plugin->menu_bar_labels, 0, sizeof plugin->menu_bar_labels);
  plugin->aqua_dock = NULL;
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->tile_preview = NULL;
  plugin->monitor_manager = NULL;
  plugin->context = NULL;
  plugin->menu_popup = NULL;
  plugin->monitors_changed_handler = 0;
  plugin->workspace_added_handler = 0;
  plugin->clock_timer = 0;
  plugin->stage_key_handler = 0;
  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;
}
