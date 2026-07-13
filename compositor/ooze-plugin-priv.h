/* compositor-internal only — not for GTK apps */
#pragma once

#include "ooze-plugin.h"
#include "ooze-aqua-menu.h"
#include "ooze-global-menu.h"

#include <meta/meta-context.h>
#include <meta/meta-monitor-manager.h>
#include <clutter/clutter.h>

struct _OozePlugin
{
  MetaPlugin parent_instance;

  ClutterActor *background_group;
  /* Interactive menu bar lives on the primary monitor. */
  ClutterActor *panel;
  ClutterActor *menu_icon;
  ClutterActor *menu_bar_labels[OOZE_GLOBAL_MENU_MAX_TOP];
  guint         n_menu_bar_labels;
  gboolean      menu_bar_from_app;
  gsize         pending_menu_index;
  gboolean      pending_menu_open;
  guint         pending_menu_idle;
  guint         pending_menu_retries;
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
  gulong        monitors_changed_handler;
  gulong        workspace_added_handler;
  guint         clock_timer;
  gulong        stage_key_handler;
  int           last_panel_width;
  int           last_dock_plate_width;
};
