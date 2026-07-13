#include "ooze-plugin-priv.h"
#include "ooze-window-chrome.h"
#include "ooze-panel.h"
#include "ooze-shell-menu.h"
#include "ooze-shared-appmenu.h"
#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-global-menu.h"
#include "ooze-desktop-icons.h"
#include "ooze-dock-shell.h"
#include "ooze-magic-lamp.h"
#include "ooze-transition.h"
#include "ooze-theme.h"
#include "ooze-window.h"
#include "ooze-xsettings.h"

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
#include <meta/meta-x11-display.h>
#include <meta/prefs.h>
#include <meta/window.h>
#include <meta/workspace.h>
#include <mtk/mtk.h>

#include <X11/Xlib.h>
#include <gio/gio.h>
#include <glib.h>
#include <string.h>
#include <time.h>

#define PANEL_HEIGHT          26.0f
#define OOZE_BUTTON_MARGIN     4.0f
#define OOZE_BUTTON_PAD_Y      2.0f

#define AQUA_DOCK_PLATE_H     58.0f
#define AQUA_DOCK_ITEM_SIZE   48.0f
#define AQUA_DOCK_PADDING     14.0f
#define AQUA_DOCK_ITEM_GAP     6.0f
#define AQUA_DOCK_REFLECT_H   26.0f
#define AQUA_DOCK_BOTTOM_GAP   8.0f
#define AQUA_DOCK_STRUT_GAP    8.0f

G_DEFINE_TYPE (OozePlugin, ooze_plugin, META_TYPE_PLUGIN)

static void ooze_plugin_on_monitors_changed (MetaMonitorManager *monitor_manager,
                                           OozePlugin           *plugin);

static MetaWindow *
ooze_plugin_get_focus_window (OozePlugin *plugin)
{
  MetaDisplay *display;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  return meta_display_get_focus_window (display);
}

static void
ooze_plugin_cancel_window_idle_ops (MetaWindowActor *actor)
{
  gpointer map_id;

  ooze_window_chrome_cancel_sync (actor);

  map_id = g_object_get_data (G_OBJECT (actor), "ooze-post-map-id");
  if (map_id)
    {
      g_source_remove (GPOINTER_TO_UINT (map_id));
      g_object_set_data (G_OBJECT (actor), "ooze-post-map-id", NULL);
    }

  ooze_window_cancel_scheduled_sync (actor);
}

/* ── Dock reflections ────────────────────────────────────────────────────── */

static gsize
ooze_plugin_count_dock_icons (OozePlugin *plugin)
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
ooze_plugin_clear_dock_reflections (OozePlugin *plugin)
{
  ClutterActor *child;

  if (!plugin->aqua_dock_reflections)
    return;

  while ((child = clutter_actor_get_first_child (plugin->aqua_dock_reflections)))
    clutter_actor_destroy (child);
}

static void
ooze_plugin_sync_dock_reflections (OozePlugin *plugin)
{
  ClutterActor *icon;
  int reflect_h;
  int logical;

  if (!plugin->aqua_dock_icons || !plugin->aqua_dock_reflections)
    return;

  ooze_plugin_clear_dock_reflections (plugin);

  logical = (int) AQUA_DOCK_ITEM_SIZE;
  reflect_h = (int) AQUA_DOCK_REFLECT_H;
  if (reflect_h < 1)
    return;

  for (icon = clutter_actor_get_first_child (plugin->aqua_dock_icons);
       icon != NULL;
       icon = clutter_actor_get_next_sibling (icon))
    {
      ClutterActor *mirror;
      ClutterContent *source;
      g_autoptr (ClutterContent) reflected = NULL;
      int texture_w = logical;
      int texture_h = logical;

      source = clutter_actor_get_content (icon);
      if (!source)
        {
          mirror = clutter_actor_new ();
          clutter_actor_set_size (mirror, (gfloat) logical, (gfloat) reflect_h);
          clutter_actor_set_reactive (mirror, FALSE);
          clutter_actor_add_child (plugin->aqua_dock_reflections, mirror);
          continue;
        }

      if (CLUTTER_IS_TEXTURE_CONTENT (source))
        {
          CoglTexture *tex;

          tex = clutter_texture_content_get_texture (CLUTTER_TEXTURE_CONTENT (source));
          if (tex)
            {
              texture_w = cogl_texture_get_width (tex);
              texture_h = cogl_texture_get_height (tex);
            }
        }

      {
        int tex_reflect_h;

        tex_reflect_h = (int) (AQUA_DOCK_REFLECT_H * (gfloat) texture_h /
                               (gfloat) MAX (logical, 1) + 0.5f);
        if (tex_reflect_h < 1)
          tex_reflect_h = 1;

        reflected = ooze_aqua_dock_reflection_content (icon, source, tex_reflect_h);
        mirror = clutter_actor_new ();
        clutter_actor_set_reactive (mirror, FALSE);
        if (reflected)
          {
            ooze_aqua_actor_set_scaled_content (mirror,
                                              g_steal_pointer (&reflected),
                                              logical,
                                              reflect_h,
                                              texture_w,
                                              tex_reflect_h);
          }
        else
          {
            clutter_actor_set_size (mirror, (gfloat) logical, (gfloat) reflect_h);
            clutter_actor_set_content (mirror, source);
            clutter_actor_set_pivot_point (mirror, 0.5f, 0.0f);
            clutter_actor_set_scale (mirror, 1.0f, -1.0f);
            clutter_actor_set_opacity (mirror, 90);
            clutter_actor_set_clip (mirror, 0.0f, 0.0f,
                                    (gfloat) logical, (gfloat) reflect_h);
          }
      }

      clutter_actor_add_child (plugin->aqua_dock_reflections, mirror);
    }
}

/* ── Dock layout ─────────────────────────────────────────────────────────── */

static void
ooze_plugin_refresh_dock_plate (OozePlugin *plugin, int plate_width)
{
  g_autoptr (ClutterContent) content = NULL;

  if (!plugin->aqua_dock_plate || plate_width < 1)
    return;

  if (plate_width == plugin->last_dock_plate_width)
    return;

  content = ooze_aqua_dock_plate_content (plugin->aqua_dock_plate,
                                        plate_width,
                                        (int) AQUA_DOCK_PLATE_H);
  if (content)
    ooze_aqua_actor_set_content (plugin->aqua_dock_plate,
                               g_steal_pointer (&content),
                               plate_width,
                               (int) AQUA_DOCK_PLATE_H);

  plugin->last_dock_plate_width = plate_width;
}

static void
ooze_plugin_update_aqua_dock_layout (OozePlugin *plugin, MetaDisplay *display)
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
  icon_count = ooze_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    icon_count = 4;

  plate_width = (gfloat) icon_count * AQUA_DOCK_ITEM_SIZE +
                ((gfloat) icon_count - 1.0f) * AQUA_DOCK_ITEM_GAP +
                2.0f * AQUA_DOCK_PADDING;
  dock_height = AQUA_DOCK_PLATE_H + AQUA_DOCK_REFLECT_H;

  x = ((gfloat) width - plate_width) / 2.0f;

  ooze_plugin_refresh_dock_plate (plugin, (int) plate_width);

  clutter_actor_set_size (plugin->aqua_dock, plate_width, dock_height);
  clutter_actor_set_position (plugin->aqua_dock,
                              x,
                              (gfloat) height - AQUA_DOCK_PLATE_H - AQUA_DOCK_BOTTOM_GAP);

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

  if (plugin->aqua_dock_reflections)
    {
      gfloat icons_width;

      icons_width = plate_width - 2.0f * AQUA_DOCK_PADDING;
      clutter_actor_set_size (plugin->aqua_dock_reflections,
                              icons_width,
                              AQUA_DOCK_REFLECT_H);
      clutter_actor_set_position (plugin->aqua_dock_reflections,
                                  AQUA_DOCK_PADDING,
                                  AQUA_DOCK_PLATE_H);
    }
}

/* ── Theme / layout ──────────────────────────────────────────────────────── */

static void
ooze_plugin_raise_chrome (OozePlugin *plugin, MetaDisplay *display)
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

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    ooze_aqua_menu_raise (plugin->menu_popup);
}

static void
ooze_plugin_update_builtin_struts (OozePlugin    *plugin G_GNUC_UNUSED,
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
  dock_h = (int) (AQUA_DOCK_PLATE_H + AQUA_DOCK_STRUT_GAP);
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
ooze_plugin_update_layout (OozePlugin *plugin, MetaDisplay *display)
{
  int width;
  int height;
  gfloat clock_width;

  meta_display_get_size (display, &width, &height);

  if (plugin->panel)
    {
      ooze_panel_refresh_texture (plugin, width);
      clutter_actor_set_size (plugin->panel, (gfloat) width, PANEL_HEIGHT);
      clutter_actor_set_position (plugin->panel, 0.0f, 0.0f);
    }

  ooze_plugin_raise_chrome (plugin, display);

  if (plugin->clock_label)
    {
      clock_width = clutter_actor_get_width (plugin->clock_label);
      clutter_actor_set_position (plugin->clock_label,
                                  (gfloat) width - clock_width - 12.0f,
                                  ((gfloat) PANEL_HEIGHT -
                                   clutter_actor_get_height (plugin->clock_label)) / 2.0f);
    }

  ooze_panel_layout_labels (plugin);

  if (plugin->menu_icon)
    {
      clutter_actor_set_position (plugin->menu_icon,
                                  OOZE_BUTTON_MARGIN,
                                  OOZE_BUTTON_PAD_Y);
    }

  ooze_plugin_update_aqua_dock_layout (plugin, display);
  ooze_plugin_update_builtin_struts (plugin, display);
}

static void
ooze_plugin_refresh_theme (OozePlugin *plugin)
{
  MetaDisplay *display;
  GList *windows;
  GList *l;

  if (!plugin->panel)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));

  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;

  ooze_panel_refresh_ooze_button (plugin);
  ooze_panel_rebuild_menu_bar (plugin);
  ooze_panel_update_clock (plugin);
  ooze_plugin_update_layout (plugin, display);
  ooze_plugin_sync_dock_reflections (plugin);

  if (plugin->monitor_manager)
    ooze_plugin_on_monitors_changed (plugin->monitor_manager, plugin);

  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      MetaWindowActor *actor;

      actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
      if (!actor || meta_window_actor_is_destroyed (actor))
        continue;

      ooze_window_chrome_invalidate (actor);
    }
  g_list_free (windows);
}

static void
ooze_plugin_on_theme_will_change (gpointer user_data)
{
  ooze_screen_transition_run (META_PLUGIN (user_data));
}

static void
ooze_plugin_on_theme_changed (gpointer user_data)
{
  ooze_plugin_refresh_theme (OOZE_PLUGIN (user_data));
}

/* ── Post-map / window lifecycle ─────────────────────────────────────────── */

static gboolean
ooze_plugin_post_map_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  g_object_set_data (G_OBJECT (actor), "ooze-post-map-id", NULL);

  ooze_window_setup (actor);

  if (g_object_get_data (G_OBJECT (actor), "ooze-window-chrome"))
    {
      ooze_window_chrome_raise_ssd (actor);
      ooze_window_chrome_schedule_sync (actor);
    }
  else
    {
      ooze_window_schedule_sync (actor);
    }

  return G_SOURCE_REMOVE;
}

static void
ooze_plugin_schedule_post_map (MetaWindowActor *actor)
{
  guint id;

  if (g_object_get_data (G_OBJECT (actor), "ooze-post-map-id"))
    return;

  g_object_ref (actor);
  id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                        ooze_plugin_post_map_idle,
                        actor,
                        g_object_unref);
  g_object_set_data (G_OBJECT (actor),
                     "ooze-post-map-id",
                     GUINT_TO_POINTER (id));
}

/* ── Dock setup ──────────────────────────────────────────────────────────── */

static gboolean
on_spot_placeholder_pressed (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             OozePlugin    *plugin)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_dock_launch_spot (plugin->context);
  return CLUTTER_EVENT_STOP;
}

static void
ooze_plugin_add_dock_placeholders (OozePlugin     *plugin,
                                 MetaDisplay  *display,
                                 ClutterActor *stage)
{
  static const struct { gfloat r, g, b; } colors[] = {
    { 0.22f, 0.48f, 0.92f },
    { 0.92f, 0.28f, 0.28f },
    { 0.28f, 0.78f, 0.38f },
    { 0.98f, 0.68f, 0.18f },
  };
  gsize i;
  int logical = (int) AQUA_DOCK_ITEM_SIZE;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  for (i = 0; i < G_N_ELEMENTS (colors); i++)
    {
      ClutterActor *item;
      g_autoptr (ClutterContent) content = NULL;

      if (i == 0)
        content = ooze_aqua_spot_icon_content (stage, display, logical);
      else
        content = ooze_aqua_dock_icon_content (stage,
                                             texture,
                                             colors[i].r,
                                             colors[i].g,
                                             colors[i].b);
      item = clutter_actor_new ();
      if (content)
        ooze_aqua_actor_set_scaled_content (item,
                                          g_steal_pointer (&content),
                                          logical,
                                          logical,
                                          texture,
                                          texture);

      if (i == 0)
        {
          clutter_actor_set_reactive (item, TRUE);
          g_signal_connect (item, "button-press-event",
                            G_CALLBACK (on_spot_placeholder_pressed), plugin);
        }

      clutter_actor_add_child (plugin->aqua_dock_icons, item);
    }
}

static void
ooze_plugin_setup_aqua_dock (OozePlugin       *plugin,
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
  clutter_box_layout_set_spacing (CLUTTER_BOX_LAYOUT (layout), AQUA_DOCK_ITEM_GAP);
  clutter_box_layout_set_homogeneous (CLUTTER_BOX_LAYOUT (layout), FALSE);
  clutter_actor_set_layout_manager (plugin->aqua_dock_icons, layout);

  plugin->aqua_dock_reflections = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->aqua_dock_reflections, FALSE);
  {
    ClutterLayoutManager *reflect_layout = clutter_box_layout_new ();
    clutter_box_layout_set_orientation (CLUTTER_BOX_LAYOUT (reflect_layout),
                                        CLUTTER_ORIENTATION_HORIZONTAL);
    clutter_box_layout_set_spacing (CLUTTER_BOX_LAYOUT (reflect_layout),
                                    AQUA_DOCK_ITEM_GAP);
    clutter_box_layout_set_homogeneous (CLUTTER_BOX_LAYOUT (reflect_layout), FALSE);
    clutter_actor_set_layout_manager (plugin->aqua_dock_reflections, reflect_layout);
  }

  if (plugin->context)
    ooze_dock_populate_container (plugin->context, display, stage,
                                plugin->aqua_dock_icons);

  icon_count = ooze_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    ooze_plugin_add_dock_placeholders (plugin, display, stage);

  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_icons);
  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_reflections);
  ooze_plugin_sync_dock_reflections (plugin);

  clutter_actor_add_child (stage, plugin->aqua_dock);
  clutter_actor_set_child_above_sibling (stage, plugin->aqua_dock, window_group);
  clutter_actor_show (plugin->aqua_dock);

  ooze_plugin_update_aqua_dock_layout (plugin, display);
}

/* ── Monitor / workspace changes ─────────────────────────────────────────── */

static void
ooze_plugin_on_monitors_changed (MetaMonitorManager *monitor_manager G_GNUC_UNUSED,
                               OozePlugin           *plugin)
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
      wallpaper = ooze_aqua_wallpaper_content (background_actor,
                                             rect.width,
                                             rect.height);
      if (wallpaper)
        ooze_aqua_actor_set_content (background_actor,
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

        desktop_icons = ooze_desktop_icons_create (plugin->context,
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

  ooze_panel_setup (plugin, display, compositor);
  ooze_plugin_setup_aqua_dock (plugin, display, compositor);
  ooze_plugin_update_layout (plugin, display);
}

static void
ooze_plugin_on_workspace_added (MetaWorkspaceManager *wm G_GNUC_UNUSED,
                              gint                  index G_GNUC_UNUSED,
                              OozePlugin             *plugin)
{
  MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (plugin));
  ooze_plugin_update_builtin_struts (plugin, display);
}

/* ── Plugin init (UI) ────────────────────────────────────────────────────── */

static void
ooze_plugin_init_ui (OozePlugin *plugin)
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
                      G_CALLBACK (ooze_plugin_on_monitors_changed),
                      plugin);
  ooze_plugin_on_monitors_changed (monitor_manager, plugin);

  {
    MetaWorkspaceManager *wm = meta_display_get_workspace_manager (display);

    if (wm && !plugin->workspace_added_handler)
      {
        plugin->workspace_added_handler =
          g_signal_connect (wm, "workspace-added",
                            G_CALLBACK (ooze_plugin_on_workspace_added),
                            plugin);
      }
  }

  ooze_theme_get_default ();
  ooze_theme_watch_will_change (NULL, ooze_plugin_on_theme_will_change, plugin);
  ooze_theme_watch (NULL, ooze_plugin_on_theme_changed, plugin);

  if (!plugin->global_menu)
    {
      plugin->global_menu = ooze_global_menu_new (display);
      ooze_global_menu_set_changed_callback (plugin->global_menu,
                                           ooze_panel_on_global_menu_changed,
                                           plugin);
      ooze_panel_rebuild_menu_bar (plugin);
    }
}

/* ── Magic lamp (minimize / unminimize) ──────────────────────────────────── */

static void
ooze_plugin_get_lamp_icon_rect (MetaPlugin      *plugin,
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
  OozePlugin *self = OOZE_PLUGIN (plugin);

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
      *out_h = AQUA_DOCK_PLATE_H;
      return;
    }

  *out_x = (gfloat) screen_w / 2.0f - 24.f;
  *out_y = (gfloat) screen_h - AQUA_DOCK_PLATE_H - AQUA_DOCK_BOTTOM_GAP;
  *out_w = 48.f;
  *out_h = AQUA_DOCK_PLATE_H;
}

static void
ooze_plugin_magic_lamp_done (MetaPlugin      *plugin,
                           MetaWindowActor *actor,
                           gboolean         unminimize,
                           gpointer         user_data G_GNUC_UNUSED)
{
  if (unminimize)
    {
      if (actor && !meta_window_actor_is_destroyed (actor))
        ooze_window_chrome_set_visible (actor, TRUE);
      if (actor)
        meta_plugin_unminimize_completed (plugin, actor);
    }
  else
    {
      if (actor && !meta_window_actor_is_destroyed (actor))
        ooze_window_chrome_set_visible (actor, FALSE);
      if (actor)
        meta_plugin_minimize_completed (plugin, actor);
    }
}

static void
ooze_plugin_run_magic_lamp (MetaPlugin      *plugin,
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

  ooze_window_chrome_set_visible (window_actor, FALSE);
  ooze_plugin_get_lamp_icon_rect (plugin, window_actor,
                                &icon_x, &icon_y, &icon_w, &icon_h);
  ooze_magic_lamp_run (plugin, window_actor, unminimize,
                     icon_x, icon_y, icon_w, icon_h,
                     ooze_plugin_magic_lamp_done, NULL);
}

/* ── MetaPlugin vfuncs ───────────────────────────────────────────────────── */

static void
ooze_plugin_minimize (MetaPlugin *plugin, MetaWindowActor *actor)
{
  MetaWindow *window = meta_window_actor_get_meta_window (actor);

  if (!window || meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    {
      meta_plugin_minimize_completed (plugin, actor);
      return;
    }

  ooze_plugin_run_magic_lamp (plugin, actor, FALSE);
}

static void
ooze_plugin_unminimize (MetaPlugin *plugin, MetaWindowActor *actor)
{
  MetaWindow *window = meta_window_actor_get_meta_window (actor);

  if (!window || meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    {
      meta_plugin_unminimize_completed (plugin, actor);
      return;
    }

  ooze_plugin_run_magic_lamp (plugin, actor, TRUE);
}

static void
ooze_plugin_map (MetaPlugin *plugin, MetaWindowActor *actor)
{
  MetaWindow *window;
  ClutterActor *window_actor;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);

  clutter_actor_show (window_actor);

  ooze_window_chrome_apply (actor, plugin);

  if (window &&
      !meta_window_is_override_redirect (window) &&
      meta_window_get_window_type (window) != META_WINDOW_DROPDOWN_MENU &&
      meta_window_get_window_type (window) != META_WINDOW_POPUP_MENU &&
      meta_window_get_window_type (window) != META_WINDOW_TOOLTIP &&
      meta_window_get_window_type (window) != META_WINDOW_NOTIFICATION &&
      meta_window_get_window_type (window) != META_WINDOW_DND &&
      meta_window_get_window_type (window) != META_WINDOW_DESKTOP)
    meta_window_focus (window, clutter_get_current_event_time ());

  meta_plugin_map_completed (plugin, actor);
  ooze_plugin_schedule_post_map (actor);
}

static void
ooze_plugin_destroy (MetaPlugin *plugin, MetaWindowActor *actor)
{
  ooze_plugin_cancel_window_idle_ops (actor);
  ooze_window_chrome_remove (actor);
  ooze_window_teardown (actor);
  meta_plugin_destroy_completed (plugin, actor);
}

static void
ooze_plugin_size_change (MetaPlugin      *plugin G_GNUC_UNUSED,
                       MetaWindowActor *actor,
                       MetaSizeChange   which_change G_GNUC_UNUSED,
                       MtkRectangle    *old_frame_rect G_GNUC_UNUSED,
                       MtkRectangle    *old_buffer_rect G_GNUC_UNUSED)
{
  ooze_window_chrome_schedule_sync (actor);
  ooze_window_schedule_sync (actor);
  meta_plugin_size_change_completed (plugin, actor);
}

/* ── Keyboard shortcuts ──────────────────────────────────────────────────── */

static gboolean
on_stage_key_press (ClutterActor *stage G_GNUC_UNUSED,
                    ClutterEvent *event,
                    OozePlugin    *plugin)
{
  ClutterModifierType state;

  if (plugin->menu_popup &&
      ooze_aqua_menu_handle_key (plugin->menu_popup, event))
    return CLUTTER_EVENT_STOP;

  state = clutter_event_get_state (event);
  if ((state & CLUTTER_SUPER_MASK) &&
      clutter_event_get_key_symbol (event) == CLUTTER_KEY_e)
    {
      ooze_dock_launch_spot (plugin->context);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

/* ── WM preferences ──────────────────────────────────────────────────────── */

/* Aqua / macOS-style: close, minimize, zoom on the left of the titlebar. */
#define OOZE_BUTTON_LAYOUT "close,minimize,maximize:"

static void
ooze_plugin_apply_button_layout (void)
{
  GSettingsSchemaSource *source;
  g_autoptr (GSettingsSchema) schema = NULL;
  g_autoptr (GSettings) wm = NULL;

  source = g_settings_schema_source_get_default ();
  if (!source)
    return;

  schema = g_settings_schema_source_lookup (source,
                                            "org.gnome.desktop.wm.preferences",
                                            TRUE);
  if (!schema || !g_settings_schema_has_key (schema, "button-layout"))
    {
      g_warning ("OozePlugin: button-layout schema missing");
      return;
    }

  wm = g_settings_new_full (schema, NULL, NULL);
  g_settings_set_string (wm, "button-layout", OOZE_BUTTON_LAYOUT);
  g_print ("OozePlugin: button-layout=%s\n", OOZE_BUTTON_LAYOUT);
}

static void
ooze_plugin_apply_wm_preferences (void)
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
      g_warning ("OozePlugin: org.gnome.mutter schema missing; edge tiling unavailable");
      return;
    }

  mutter = g_settings_new_full (schema, NULL, NULL);

  if (g_settings_schema_has_key (schema, "edge-tiling"))
    g_settings_set_boolean (mutter, "edge-tiling", TRUE);

  if (g_settings_schema_has_key (schema, "draggable-border-width"))
    g_settings_set_int (mutter, "draggable-border-width", 12);

  ooze_plugin_apply_button_layout ();

  g_settings_sync ();

  meta_prefs_set_show_fallback_app_menu (FALSE);

  g_print ("OozePlugin: edge-tiling pref=%s settings=%s draggable-border-width=%d global-app-menu=%s\n",
           meta_prefs_get_edge_tiling () ? "on" : "off",
           g_settings_get_boolean (mutter, "edge-tiling") ? "on" : "off",
           g_settings_schema_has_key (schema, "draggable-border-width")
             ? g_settings_get_int (mutter, "draggable-border-width") : -1,
           meta_prefs_get_show_fallback_app_menu () ? "fallback" : "exported");
}

/* ── Tile preview ────────────────────────────────────────────────────────── */

static void
ooze_plugin_show_tile_preview (MetaPlugin   *plugin,
                             MetaWindow   *window G_GNUC_UNUSED,
                             MtkRectangle *tile_rect,
                             int           tile_monitor_number G_GNUC_UNUSED)
{
  OozePlugin *self = OOZE_PLUGIN (plugin);
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
ooze_plugin_hide_tile_preview (MetaPlugin *plugin)
{
  OozePlugin *self = OOZE_PLUGIN (plugin);

  if (self->tile_preview)
    clutter_actor_hide (self->tile_preview);
}

/* ── XSettings xevent bridge ─────────────────────────────────────────────── */

static void
ooze_plugin_on_xsettings_xevent (MetaX11Display *x11_display G_GNUC_UNUSED,
                               XEvent         *xev,
                               gpointer        user_data G_GNUC_UNUSED)
{
  ooze_xsettings_handle_xevent (xev);
}

/* ── Plugin start ────────────────────────────────────────────────────────── */

static void
ooze_plugin_start (MetaPlugin *plugin)
{
  OozePlugin *self = OOZE_PLUGIN (plugin);
  MetaDisplay *display;
  MetaX11Display *x11_display;
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage;

  g_print ("OozePlugin: compositor plugin started successfully\n");

  ooze_appmenu_setup_environment ();
  ooze_appmenu_ensure_registrar ();

  display = meta_plugin_get_display (plugin);
  x11_display = meta_display_get_x11_display (display);
  if (x11_display)
    {
      Display *xdpy = meta_x11_display_get_xdisplay (x11_display);
      if (xdpy)
        {
          const char *name = DisplayString (xdpy);

          if (!name || !*name)
            name = ":0";
          if (ooze_xsettings_ensure_with_xdisplay (xdpy, name, FALSE))
            {
              meta_x11_display_add_event_func (x11_display,
                                               ooze_plugin_on_xsettings_xevent,
                                               NULL, NULL);
              ooze_appmenu_ensure_shell_shows_menubar_on_display (name);
            }
        }
    }

  ooze_plugin_apply_wm_preferences ();
  ooze_plugin_init_ui (self);

  display = meta_plugin_get_display (plugin);
  context = meta_display_get_context (display);
  backend = meta_context_get_backend (context);
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

  if (!self->stage_key_handler)
    {
      self->stage_key_handler =
        g_signal_connect (stage, "key-press-event",
                          G_CALLBACK (on_stage_key_press), self);
    }

  clutter_actor_show (stage);
}

/* ── GObject lifecycle ───────────────────────────────────────────────────── */

static void
ooze_plugin_dispose (GObject *object)
{
  OozePlugin *plugin = OOZE_PLUGIN (object);

  ooze_panel_dispose (plugin);

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

  ooze_theme_unwatch_will_change (NULL, ooze_plugin_on_theme_will_change, plugin);
  ooze_theme_unwatch (NULL, ooze_plugin_on_theme_changed, plugin);

  ooze_aqua_menu_destroy (plugin->menu_popup);
  plugin->menu_popup = NULL;

  ooze_global_menu_free (plugin->global_menu);
  plugin->global_menu = NULL;

  g_clear_pointer (&plugin->aqua_dock, clutter_actor_destroy);
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->aqua_dock_reflections = NULL;
  g_clear_pointer (&plugin->tile_preview, clutter_actor_destroy);
  g_clear_pointer (&plugin->background_group, clutter_actor_destroy);
  g_clear_object (&plugin->context);

  G_OBJECT_CLASS (ooze_plugin_parent_class)->dispose (object);
}

static void
ooze_plugin_class_init (OozePluginClass *klass)
{
  MetaPluginClass *plugin_class = META_PLUGIN_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ooze_plugin_dispose;

  plugin_class->start = ooze_plugin_start;
  plugin_class->map = ooze_plugin_map;
  plugin_class->destroy = ooze_plugin_destroy;
  plugin_class->size_change = ooze_plugin_size_change;
  plugin_class->minimize = ooze_plugin_minimize;
  plugin_class->unminimize = ooze_plugin_unminimize;
  plugin_class->show_tile_preview = ooze_plugin_show_tile_preview;
  plugin_class->hide_tile_preview = ooze_plugin_hide_tile_preview;
}

static void
ooze_plugin_init (OozePlugin *plugin)
{
  plugin->background_group = NULL;
  plugin->panel = NULL;
  plugin->menu_icon = NULL;
  plugin->clock_label = NULL;
  memset (plugin->menu_bar_labels, 0, sizeof plugin->menu_bar_labels);
  plugin->aqua_dock = NULL;
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->aqua_dock_reflections = NULL;
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
