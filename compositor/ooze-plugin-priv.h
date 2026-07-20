/* compositor-internal only — not for GTK apps */
#pragma once

#include "ooze-plugin.h"
#include "ooze-aqua-menu.h"
#include "ooze-global-menu.h"
#include "ooze-notifications.h"
#include "ooze-shot.h"

#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <clutter/clutter.h>
#include <gio/gio.h>

struct _OozePlugin
{
  MetaPlugin parent_instance;

  gboolean      shutting_down;
  ClutterActor *background_group;
  /* Interactive menu bar lives on the primary monitor. */
  ClutterActor *panel;
  ClutterActor *menu_icon;
  ClutterActor *menu_bar_labels[OOZE_GLOBAL_MENU_MAX_TOP];
  guint         n_menu_bar_labels;
  gboolean      menu_bar_from_app;
  gboolean      menu_bar_fallback;
  gsize         pending_menu_index;
  gboolean      pending_menu_open;
  guint         pending_menu_idle;
  guint         pending_menu_retries;
  guint         logout_idle;
  guint         menubar_rebuild_idle;
  gboolean      menu_bar_needs_rebuild;
  gboolean      force_shell_menu;
  ClutterActor *clock_label;
  /* Visual-only menu bars on non-primary monitors (one panel per desktop). */
  ClutterActor **aux_panels;
  guint         n_aux_panels;
  int          *aux_panel_widths;
  ClutterActor *aqua_dock;
  ClutterActor *aqua_dock_plate;
  ClutterActor *aqua_dock_icons;
  ClutterActor *aqua_dock_reflections;
  ClutterActor *tile_preview;
  MetaMonitorManager *monitor_manager;
  MetaContext  *context;
  OozeAquaMenu   *menu_popup;
  OozeGlobalMenu *global_menu;
  struct _OozeAppmenuWayland *wl_appmenu;
  OozeNotifications *notifications;
  OozeShot         *shot;
  struct _OozeForeignGelState *foreign_gel;
  gulong        monitors_changed_handler;
  gulong        workspace_added_handler;
  gulong        x11_display_opened_handler;
  guint         xsettings_retry_id;
  guint         xsettings_retry_tries;
  guint         clock_timer;
  gulong        stage_key_handler;
  int           last_panel_width;
  int           last_dock_plate_width;
  guint         dock_reflect_idle;
  guint         chrome_theme_idle;

  /* Lock screen (compositor overlay + PAM helper) */
  gboolean      locked;
  gboolean      lock_enabled;
  ClutterActor *lock_overlay;
  ClutterActor *lock_card;
  ClutterActor *lock_clock_label;
  ClutterActor *lock_user_label;
  ClutterActor *lock_entry_box;
  ClutterActor *lock_password;
  ClutterActor *lock_unlock_btn;
  ClutterActor *lock_status_label;
  ClutterGrab  *lock_grab;
  GSubprocess  *lock_auth_proc;
  guint         lock_clock_timer;
  guint         lock_fade_id;
  guint         lock_idle_watch_id;
  guint         lock_logind_sub_id;
  GDBusConnection *lock_logind_conn;
  GSettings    *session_settings;
  GSettings    *screensaver_settings;
  GSettings    *background_settings;
  GSettings    *scenery_settings;

  /* Screensaver (black compositor overlay hosting an XScreenSaver hack) */
  gboolean      screensaver_active;
  gboolean      screensaver_black;   /* overlay reached full black */
  ClutterActor *screensaver_overlay;
  guint         screensaver_idle_watch_id;
  guint         screensaver_user_active_watch_id;
  guint         screensaver_arm_id;
  guint         screensaver_fade_id;
  guint         screensaver_phase_id;
  gulong        screensaver_stage_capture_id;
  gboolean      screensaver_input_armed;

  /* XScreenSaver hack backdrop (X11 hack adopted into the overlay) */
  GPid          saver_hack_pid;
  char         *saver_hack_name;
  guint         saver_hack_child_watch_id;
  guint         saver_hack_kill_id;
  ClutterActor *saver_hack_clone;
  gpointer      saver_hack_window; /* MetaWindow* */
  gboolean      saver_unredirect_disabled;

  /* StatusNotifier tray (AppIndicator host) */
  ClutterActor *tray_box;
  GPtrArray    *tray_icons; /* OozeTrayIcon* */
  gpointer      sni_watcher; /* OozeSniWatcher* */
  gpointer      tray_menu_ctx;
  OozeAquaMenu *tray_popup;
  guint         tray_appearance_idle;
};

void ooze_plugin_get_active_monitor_geometry (MetaDisplay  *display,
                                               MtkRectangle *rect_out);

void ooze_plugin_begin_shutdown (OozePlugin *plugin);
