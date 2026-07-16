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
#include "ooze-stall.h"
#include "ooze-lock.h"
#include "ooze-tray.h"
#include "ooze-notifications.h"
#include "ooze-shot.h"
#include "ooze-portal-env.h"
#include "ooze-autostart.h"
#include "ooze-polkit.h"
#include "ooze-session-dialog.h"
#include "ooze-foreign-gel.h"

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
  ooze_window_chrome_cancel_sync (actor);
  ooze_window_cancel_scheduled_sync (actor);
}

static void
ooze_plugin_cancel_desktop_interactions (OozePlugin *plugin)
{
  ClutterActor *background;
  ClutterActor *child;

  if (!plugin->background_group)
    return;

  for (background = clutter_actor_get_first_child (plugin->background_group);
       background != NULL;
       background = clutter_actor_get_next_sibling (background))
    {
      for (child = clutter_actor_get_first_child (background);
           child != NULL;
           child = clutter_actor_get_next_sibling (child))
        ooze_desktop_icons_begin_shutdown (child);
    }
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

/*
 * Dock shelf reflections. Default uses a cheap Clutter flip (no GPU
 * texture download / Cairo). OOZE_DOCK_HQ_REFLECTIONS=1 restores the
 * older cogl_texture_get_data path (expensive; idle-only).
 */
static gboolean
ooze_plugin_dock_hq_reflections (void)
{
  const char *v = g_getenv ("OOZE_DOCK_HQ_REFLECTIONS");

  if (!v || !*v)
    return FALSE;
  return g_ascii_strcasecmp (v, "1") == 0 ||
         g_ascii_strcasecmp (v, "true") == 0 ||
         g_ascii_strcasecmp (v, "yes") == 0 ||
         g_ascii_strcasecmp (v, "on") == 0;
}

static void
ooze_plugin_sync_dock_reflections (OozePlugin *plugin)
{
  g_autoptr (OozeStallScope) stall = NULL;
  ClutterActor *icon;
  int reflect_h;
  int logical;
  gboolean hq;

  if (!plugin->aqua_dock_icons || !plugin->aqua_dock_reflections)
    return;

  stall = ooze_stall_begin ("dock-reflections");
  ooze_plugin_clear_dock_reflections (plugin);

  logical = (int) AQUA_DOCK_ITEM_SIZE;
  reflect_h = (int) AQUA_DOCK_REFLECT_H;
  if (reflect_h < 1)
    return;

  hq = ooze_plugin_dock_hq_reflections ();

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
      mirror = clutter_actor_new ();
      clutter_actor_set_reactive (mirror, FALSE);

      if (!source)
        {
          clutter_actor_set_size (mirror, (gfloat) logical, (gfloat) reflect_h);
          clutter_actor_add_child (plugin->aqua_dock_reflections, mirror);
          continue;
        }

      if (hq && CLUTTER_IS_TEXTURE_CONTENT (source))
        {
          CoglTexture *tex;

          tex = clutter_texture_content_get_texture (CLUTTER_TEXTURE_CONTENT (source));
          if (tex)
            {
              texture_w = cogl_texture_get_width (tex);
              texture_h = cogl_texture_get_height (tex);
            }

          {
            int tex_reflect_h;

            tex_reflect_h = (int) (AQUA_DOCK_REFLECT_H * (gfloat) texture_h /
                                   (gfloat) MAX (logical, 1) + 0.5f);
            if (tex_reflect_h < 1)
              tex_reflect_h = 1;

            reflected = ooze_aqua_dock_reflection_content (icon, source,
                                                          tex_reflect_h);
            if (reflected)
              {
                ooze_aqua_actor_set_scaled_content (mirror,
                                                    g_steal_pointer (&reflected),
                                                    logical,
                                                    reflect_h,
                                                    texture_w,
                                                    tex_reflect_h);
                clutter_actor_add_child (plugin->aqua_dock_reflections, mirror);
                continue;
              }
          }
        }

      /*
       * Cheap path: flip the existing icon content (no GPU readback).
       *
       * Pivot at the top of a strip sized reflect_h flips paint UP into the
       * icon band. Instead: full-size icon child bottom-aligned to the shelf
       * (y=0 of this strip), flipped around that bottom edge so content
       * extends downward; outer clips to the reflection strip below the plate.
       */
      {
        ClutterActor *inner;

        clutter_actor_set_size (mirror, (gfloat) logical, (gfloat) reflect_h);
        clutter_actor_set_clip_to_allocation (mirror, TRUE);
        clutter_actor_set_opacity (mirror, 90);

        inner = clutter_actor_new ();
        clutter_actor_set_reactive (inner, FALSE);
        clutter_actor_set_size (inner, (gfloat) logical, (gfloat) logical);
        clutter_actor_set_content (inner, source);
        /* Bottom of upright icon sits on the shelf line (parent y=0). */
        clutter_actor_set_position (inner, 0.0f, (gfloat) (-logical));
        clutter_actor_set_pivot_point (inner, 0.5f, 1.0f);
        clutter_actor_set_scale (inner, 1.0f, -1.0f);
        clutter_actor_add_child (mirror, inner);
      }
      clutter_actor_add_child (plugin->aqua_dock_reflections, mirror);
    }
}

static gboolean
ooze_plugin_dock_reflections_idle (gpointer user_data)
{
  OozePlugin *plugin = user_data;

  plugin->dock_reflect_idle = 0;
  ooze_plugin_sync_dock_reflections (plugin);
  return G_SOURCE_REMOVE;
}

static void
ooze_plugin_schedule_dock_reflections (OozePlugin *plugin)
{
  if (!plugin)
    return;
  if (plugin->dock_reflect_idle)
    return;

  plugin->dock_reflect_idle =
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                     ooze_plugin_dock_reflections_idle,
                     plugin,
                     NULL);
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
ooze_plugin_get_primary_monitor_geometry (MetaDisplay  *display,
                                          MtkRectangle *rect_out)
{
  int primary;
  int n_monitors;

  g_return_if_fail (rect_out != NULL);

  n_monitors = meta_display_get_n_monitors (display);
  if (n_monitors < 1)
    {
      meta_display_get_size (display, &rect_out->width, &rect_out->height);
      rect_out->x = 0;
      rect_out->y = 0;
      return;
    }

  primary = meta_display_get_primary_monitor (display);
  if (primary < 0 || primary >= n_monitors)
    primary = 0;

  meta_display_get_monitor_geometry (display, primary, rect_out);
}

static void
ooze_plugin_clear_aux_panels (OozePlugin *plugin)
{
  guint i;

  for (i = 0; i < plugin->n_aux_panels; i++)
    g_clear_pointer (&plugin->aux_panels[i], clutter_actor_destroy);

  g_clear_pointer (&plugin->aux_panels, g_free);
  g_clear_pointer (&plugin->aux_panel_widths, g_free);
  plugin->n_aux_panels = 0;
}

static void
ooze_plugin_refresh_aux_panel_texture (OozePlugin    *plugin,
                                       guint        index,
                                       ClutterActor *panel,
                                       int          width)
{
  g_autoptr (ClutterContent) content = NULL;

  if (!panel || width < 1)
    return;

  if (plugin->aux_panel_widths &&
      plugin->aux_panel_widths[index] == width)
    return;

  content = ooze_aqua_pinstripe_content (panel, width, (int) PANEL_HEIGHT);
  if (content)
    ooze_aqua_actor_set_content (panel,
                                 g_steal_pointer (&content),
                                 width,
                                 (int) PANEL_HEIGHT);

  if (plugin->aux_panel_widths)
    plugin->aux_panel_widths[index] = width;
}

static void
ooze_plugin_sync_aux_panels (OozePlugin       *plugin,
                             MetaDisplay    *display,
                             MetaCompositor *compositor)
{
  ClutterActor *stage;
  ClutterActor *window_group;
  int n_monitors;
  int primary;
  int i;
  guint aux_needed;
  guint aux_i;

  n_monitors = meta_display_get_n_monitors (display);
  if (n_monitors < 1)
    {
      ooze_plugin_clear_aux_panels (plugin);
      return;
    }

  primary = meta_display_get_primary_monitor (display);
  if (primary < 0 || primary >= n_monitors)
    primary = 0;

  aux_needed = (guint) MAX (0, n_monitors - 1);
  if (aux_needed != plugin->n_aux_panels)
    {
      ooze_plugin_clear_aux_panels (plugin);
      if (aux_needed == 0)
        return;

      plugin->aux_panels = g_new0 (ClutterActor *, aux_needed);
      plugin->aux_panel_widths = g_new0 (int, aux_needed);
      plugin->n_aux_panels = aux_needed;

      stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
      window_group = meta_compositor_get_window_group (compositor);

      for (aux_i = 0; aux_i < aux_needed; aux_i++)
        {
          ClutterActor *panel = clutter_actor_new ();

          clutter_actor_set_reactive (panel, FALSE);
          clutter_actor_add_child (stage, panel);
          clutter_actor_set_child_above_sibling (stage, panel, window_group);
          clutter_actor_show (panel);
          plugin->aux_panels[aux_i] = panel;
        }
    }

  aux_i = 0;
  for (i = 0; i < n_monitors; i++)
    {
      MtkRectangle rect;
      ClutterActor *panel;

      if (i == primary)
        continue;

      if (aux_i >= plugin->n_aux_panels)
        break;

      meta_display_get_monitor_geometry (display, i, &rect);
      panel = plugin->aux_panels[aux_i];
      ooze_plugin_refresh_aux_panel_texture (plugin, aux_i, panel, rect.width);
      clutter_actor_set_size (panel, (gfloat) rect.width, PANEL_HEIGHT);
      clutter_actor_set_position (panel, (gfloat) rect.x, (gfloat) rect.y);
      aux_i++;
    }
}

static void
ooze_plugin_update_aqua_dock_layout (OozePlugin *plugin, MetaDisplay *display)
{
  MtkRectangle primary;
  gsize icon_count;
  gfloat plate_width;
  gfloat dock_height;
  gfloat x;

  if (!plugin->aqua_dock)
    return;

  ooze_plugin_get_primary_monitor_geometry (display, &primary);
  icon_count = ooze_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    icon_count = 4;

  plate_width = (gfloat) icon_count * AQUA_DOCK_ITEM_SIZE +
                ((gfloat) icon_count - 1.0f) * AQUA_DOCK_ITEM_GAP +
                2.0f * AQUA_DOCK_PADDING;
  dock_height = AQUA_DOCK_PLATE_H + AQUA_DOCK_REFLECT_H;

  /* Dock stays on the main (primary) desktop only. */
  x = (gfloat) primary.x + ((gfloat) primary.width - plate_width) / 2.0f;

  ooze_plugin_refresh_dock_plate (plugin, (int) plate_width);

  clutter_actor_set_size (plugin->aqua_dock, plate_width, dock_height);
  clutter_actor_set_position (plugin->aqua_dock,
                              x,
                              (gfloat) primary.y + (gfloat) primary.height -
                              AQUA_DOCK_PLATE_H - AQUA_DOCK_BOTTOM_GAP);

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
  guint i;

  if (!plugin->panel && !plugin->aqua_dock && plugin->n_aux_panels == 0)
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

  for (i = 0; i < plugin->n_aux_panels; i++)
    {
      if (!plugin->aux_panels[i])
        continue;
      clutter_actor_set_child_above_sibling (stage, plugin->aux_panels[i], above);
      above = plugin->aux_panels[i];
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
  int n_monitors;
  int primary;
  int panel_h;
  int dock_h;
  int i;

  if (!display)
    return;

  n_monitors = meta_display_get_n_monitors (display);
  if (n_monitors < 1)
    return;

  primary = meta_display_get_primary_monitor (display);
  if (primary < 0 || primary >= n_monitors)
    primary = 0;

  panel_h = (int) PANEL_HEIGHT;
  dock_h = (int) (AQUA_DOCK_PLATE_H + AQUA_DOCK_STRUT_GAP);

  wm = meta_display_get_workspace_manager (display);
  if (!wm)
    return;

  workspaces = meta_workspace_manager_get_workspaces (wm);
  for (l = workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      GSList *struts = NULL;

      for (i = 0; i < n_monitors; i++)
        {
          MtkRectangle rect;
          MetaStrut *top;

          meta_display_get_monitor_geometry (display, i, &rect);
          if (panel_h >= rect.height)
            continue;

          top = g_new0 (MetaStrut, 1);
          top->side = META_SIDE_TOP;
          top->rect.x = rect.x;
          top->rect.y = rect.y;
          top->rect.width = rect.width;
          top->rect.height = panel_h;
          struts = g_slist_prepend (struts, top);

          /* Bottom dock strut only on the main desktop. */
          if (i == primary && panel_h + dock_h < rect.height)
            {
              MetaStrut *bottom = g_new0 (MetaStrut, 1);

              bottom->side = META_SIDE_BOTTOM;
              bottom->rect.x = rect.x;
              bottom->rect.y = rect.y + rect.height - dock_h;
              bottom->rect.width = rect.width;
              bottom->rect.height = dock_h;
              struts = g_slist_prepend (struts, bottom);
            }
        }

      meta_workspace_set_builtin_struts (workspace, struts);
      g_slist_free_full (struts, g_free);
    }
}

static void
ooze_plugin_update_layout (OozePlugin *plugin, MetaDisplay *display)
{
  MetaCompositor *compositor;
  MtkRectangle primary;
  gfloat clock_width;

  compositor = meta_display_get_compositor (display);
  ooze_plugin_get_primary_monitor_geometry (display, &primary);

  if (plugin->panel)
    {
      ooze_panel_refresh_texture (plugin, primary.width);
      clutter_actor_set_size (plugin->panel,
                              (gfloat) primary.width,
                              PANEL_HEIGHT);
      clutter_actor_set_position (plugin->panel,
                                  (gfloat) primary.x,
                                  (gfloat) primary.y);
    }

  ooze_plugin_sync_aux_panels (plugin, display, compositor);
  ooze_plugin_raise_chrome (plugin, display);

  if (plugin->clock_label)
    {
      clock_width = clutter_actor_get_width (plugin->clock_label);
      clutter_actor_set_position (plugin->clock_label,
                                  (gfloat) primary.width - clock_width - 12.0f,
                                  ((gfloat) PANEL_HEIGHT -
                                   clutter_actor_get_height (plugin->clock_label)) / 2.0f);
    }
  ooze_tray_layout (plugin, (gfloat) primary.width, (gfloat) PANEL_HEIGHT);

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
ooze_plugin_refresh_wallpapers (OozePlugin *plugin)
{
  ClutterActor *child;

  if (!plugin->background_group)
    return;

  /*
   * Repaint wallpaper cloth for the new palette only. Do NOT call
   * ooze_plugin_on_monitors_changed here — that destroys desktop icons and
   * reloads every themed icon on the main thread, which freezes launches
   * after Light↔Dark (clicks still reach clients under the fade overlay).
   */
  for (child = clutter_actor_get_first_child (plugin->background_group);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      g_autoptr (ClutterContent) wallpaper = NULL;
      int width = (int) clutter_actor_get_width (child);
      int height = (int) clutter_actor_get_height (child);

      if (width < 1 || height < 1)
        continue;

      wallpaper = ooze_aqua_wallpaper_content (child, width, height);
      if (wallpaper)
        ooze_aqua_actor_set_content (child,
                                     g_steal_pointer (&wallpaper),
                                     width,
                                     height);
    }
}

static gboolean
ooze_plugin_chrome_theme_idle (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  MetaDisplay *display;
  GList *windows;
  GList *l;

  plugin->chrome_theme_idle = 0;
  if (plugin->shutting_down)
    return G_SOURCE_REMOVE;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return G_SOURCE_REMOVE;

  /* Only SSD Gel frames — skip clients without compositor chrome. */
  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      MetaWindowActor *actor;

      actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
      if (!actor || meta_window_actor_is_destroyed (actor))
        continue;
      if (!g_object_get_data (G_OBJECT (actor), "ooze-window-chrome"))
        continue;

      ooze_window_chrome_invalidate (actor);
    }
  g_list_free (windows);
  return G_SOURCE_REMOVE;
}

static void
ooze_plugin_schedule_chrome_theme_refresh (OozePlugin *plugin)
{
  if (plugin->chrome_theme_idle != 0)
    return;

  plugin->chrome_theme_idle =
    g_idle_add_full (G_PRIORITY_LOW,
                     ooze_plugin_chrome_theme_idle,
                     plugin,
                     NULL);
}

static void
ooze_plugin_refresh_theme (OozePlugin *plugin)
{
  g_autoptr (OozeStallScope) stall = NULL;
  MetaDisplay *display;

  if (!plugin->panel)
    return;

  stall = ooze_stall_begin ("theme-refresh");
  display = meta_plugin_get_display (META_PLUGIN (plugin));

  /*
   * Keep this path cheap. Do NOT:
   *  - rebuild dock icons / destroy aqua_dock_icons (MetaContext lives there)
   *  - sync dock reflections (cairo+cogl download per icon; icons unchanged)
   *  - full menu-bar destroy/recreate (recolor in place)
   *  - sync Gel decorations on the click/GSettings stack (idle below)
   */
  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;
  if (plugin->aux_panel_widths && plugin->n_aux_panels > 0)
    memset (plugin->aux_panel_widths, 0,
            sizeof (int) * plugin->n_aux_panels);

  ooze_panel_refresh_ooze_button (plugin);
  ooze_panel_recolor_menu_bar (plugin);
  ooze_tray_refresh_appearance (plugin);
  ooze_plugin_update_layout (plugin, display);
  ooze_plugin_refresh_wallpapers (plugin);
  ooze_plugin_schedule_chrome_theme_refresh (plugin);
}

static void
ooze_plugin_on_theme_will_change (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (plugin->shutting_down)
    return;

  ooze_screen_transition_run (META_PLUGIN (user_data));
}

static void
ooze_plugin_on_theme_changed (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    ooze_plugin_refresh_theme (plugin);
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
ooze_plugin_dock_icons_changed (gpointer user_data)
{
  OozePlugin *plugin = user_data;
  MetaDisplay *display;

  if (!plugin || plugin->shutting_down)
    return;
  display = meta_plugin_get_display (META_PLUGIN (plugin));
  plugin->last_dock_plate_width = 0;
  /* Reflections after icons; never block the dock rebuild callback. */
  ooze_plugin_schedule_dock_reflections (plugin);
  ooze_plugin_update_aqua_dock_layout (plugin, display);
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
  /* Keep flipped overflow inside the below-plate strip. */
  clutter_actor_set_clip_to_allocation (plugin->aqua_dock_reflections, TRUE);
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

  ooze_dock_set_changed_callback (plugin->aqua_dock_icons,
                                  (OozeDockChangedFn) ooze_plugin_dock_icons_changed,
                                  plugin);

  icon_count = ooze_plugin_count_dock_icons (plugin);
  if (icon_count == 0)
    ooze_plugin_add_dock_placeholders (plugin, display, stage);

  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_icons);
  clutter_actor_add_child (plugin->aqua_dock, plugin->aqua_dock_reflections);
  ooze_plugin_schedule_dock_reflections (plugin);

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
  ooze_tray_setup (plugin);
  ooze_plugin_setup_aqua_dock (plugin, display, compositor);
  ooze_plugin_update_layout (plugin, display);
  ooze_notifications_reflow (plugin->notifications);
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
      ooze_panel_schedule_rebuild (plugin);
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
  if (OOZE_PLUGIN (plugin)->shutting_down)
    {
      if (unminimize)
        meta_plugin_unminimize_completed (plugin, actor);
      else
        meta_plugin_minimize_completed (plugin, actor);
      return;
    }

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
  if (OOZE_PLUGIN (plugin)->shutting_down)
    {
      meta_plugin_minimize_completed (plugin, actor);
      return;
    }

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
  if (OOZE_PLUGIN (plugin)->shutting_down)
    {
      meta_plugin_unminimize_completed (plugin, actor);
      return;
    }

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

  if (OOZE_PLUGIN (plugin)->shutting_down)
    {
      meta_plugin_map_completed (plugin, actor);
      return;
    }

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
}

static void
ooze_plugin_destroy (MetaPlugin *plugin, MetaWindowActor *actor)
{
  if (OOZE_PLUGIN (plugin)->shutting_down)
    {
      meta_plugin_destroy_completed (plugin, actor);
      return;
    }

  ooze_plugin_cancel_window_idle_ops (actor);
  ooze_window_chrome_remove (actor);
  ooze_window_teardown (actor);
  meta_plugin_destroy_completed (plugin, actor);
}

static void
ooze_plugin_size_change (MetaPlugin      *plugin G_GNUC_UNUSED,
                       MetaWindowActor *actor G_GNUC_UNUSED,
                       MetaSizeChange   which_change G_GNUC_UNUSED,
                       MtkRectangle    *old_frame_rect G_GNUC_UNUSED,
                       MtkRectangle    *old_buffer_rect G_GNUC_UNUSED)
{
  /* Mutter owns tile/maximize geometry. No grab-overlay / SSD sync here. */
  meta_plugin_size_change_completed (plugin, actor);
}

/* ── Keyboard shortcuts ──────────────────────────────────────────────────── */

static gboolean
on_stage_key_press (ClutterActor *stage G_GNUC_UNUSED,
                    ClutterEvent *event,
                    OozePlugin    *plugin)
{
  ClutterModifierType state;

  if (plugin->shutting_down)
    return CLUTTER_EVENT_STOP;

  if (plugin->locked)
    return CLUTTER_EVENT_PROPAGATE;

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

  if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_Print)
    {
      g_autoptr (GError) error = NULL;

      if (!ooze_shot_capture_desktop (plugin->shot, &error))
        g_warning ("Ooze Shot: capture failed: %s",
                   error ? error->message : "unknown");
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

  {
    g_autoptr (GSettingsSchema) kb_schema = NULL;
    g_autoptr (GSettings) kb = NULL;

    kb_schema = g_settings_schema_source_lookup (source,
                                                 "org.gnome.mutter.keybindings",
                                                 TRUE);
    if (kb_schema)
      {
        kb = g_settings_new_full (kb_schema, NULL, NULL);
        if (g_settings_schema_has_key (kb_schema, "toggle-tiled-left"))
          {
            const char *left[] = { "<Super>Left", NULL };
            g_settings_set_strv (kb, "toggle-tiled-left", left);
          }
        if (g_settings_schema_has_key (kb_schema, "toggle-tiled-right"))
          {
            const char *right[] = { "<Super>Right", NULL };
            g_settings_set_strv (kb, "toggle-tiled-right", right);
          }
      }
  }

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
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage;
  CoglColor color;

  if (!tile_rect || tile_rect->width < 1 || tile_rect->height < 1)
    return;

  display = meta_plugin_get_display (plugin);
  if (!display)
    return;
  context = meta_display_get_context (display);
  if (!context)
    return;
  backend = meta_context_get_backend (context);
  if (!backend)
    return;
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
  if (!stage)
    return;

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

static gboolean
ooze_plugin_try_setup_xsettings (OozePlugin *self)
{
  MetaDisplay *display;
  MetaX11Display *x11_display;
  Display *xdpy;
  const char *name;

  display = meta_plugin_get_display (META_PLUGIN (self));
  if (!display)
    return FALSE;

  x11_display = meta_display_get_x11_display (display);
  if (!x11_display)
    return FALSE;

  xdpy = meta_x11_display_get_xdisplay (x11_display);
  if (!xdpy)
    return FALSE;

  name = DisplayString (xdpy);
  if (!name || !*name)
    name = ":0";

  if (ooze_xsettings_ensure_with_xdisplay (xdpy, name, FALSE))
    {
      meta_x11_display_add_event_func (x11_display,
                                       ooze_plugin_on_xsettings_xevent,
                                       NULL, NULL);
      ooze_appmenu_ensure_shell_shows_menubar_on_display (name);
      return TRUE;
    }

  g_warning ("OozePlugin: XSETTINGS ensure failed on %s", name);
  ooze_appmenu_ensure_shell_shows_menubar_on_display (name);
  return FALSE;
}

static void
ooze_plugin_cancel_xsettings_retry (OozePlugin *self)
{
  if (self->xsettings_retry_id)
    {
      g_source_remove (self->xsettings_retry_id);
      self->xsettings_retry_id = 0;
    }
}

static gboolean
ooze_plugin_xsettings_retry_cb (gpointer user_data)
{
  OozePlugin *self = OOZE_PLUGIN (user_data);

  if (self->shutting_down)
    {
      self->xsettings_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  if (ooze_plugin_try_setup_xsettings (self))
    {
      self->xsettings_retry_id = 0;
      self->xsettings_retry_tries = 0;
      if (self->x11_display_opened_handler)
        {
          MetaDisplay *display =
            meta_plugin_get_display (META_PLUGIN (self));

          if (display)
            g_signal_handler_disconnect (display,
                                         self->x11_display_opened_handler);
          self->x11_display_opened_handler = 0;
        }
      return G_SOURCE_REMOVE;
    }

  self->xsettings_retry_tries++;
  if (self->xsettings_retry_tries >= 40) /* ~10s */
    {
      g_warning ("OozePlugin: Meta X11 display never became ready; "
                 "GTK3 global menus (Inkscape) will not hide/export");
      self->xsettings_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
ooze_plugin_on_x11_display_opened (MetaDisplay *display G_GNUC_UNUSED,
                                   gpointer     user_data)
{
  OozePlugin *self = OOZE_PLUGIN (user_data);

  if (self->shutting_down)
    return;

  /* DISPLAY exists now; refresh the activation environments. */
  ooze_portal_env_publish ();

  if (ooze_plugin_try_setup_xsettings (self))
    {
      ooze_plugin_cancel_xsettings_retry (self);
      if (self->x11_display_opened_handler)
        {
          g_signal_handler_disconnect (display,
                                       self->x11_display_opened_handler);
          self->x11_display_opened_handler = 0;
        }
    }
}

static void
ooze_plugin_schedule_xsettings (OozePlugin *self)
{
  MetaDisplay *display;

  if (ooze_plugin_try_setup_xsettings (self))
    return;

  display = meta_plugin_get_display (META_PLUGIN (self));
  g_print ("OozePlugin: waiting for Meta X11 display (Xwayland) "
           "before serving ShellShowsMenubar\n");

  if (display && !self->x11_display_opened_handler)
    {
      self->x11_display_opened_handler =
        g_signal_connect (display, "x11-display-opened",
                          G_CALLBACK (ooze_plugin_on_x11_display_opened),
                          self);
    }

  if (!self->xsettings_retry_id)
    {
      self->xsettings_retry_tries = 0;
      self->xsettings_retry_id =
        g_timeout_add (250, ooze_plugin_xsettings_retry_cb, self);
    }
}

/* ── Plugin start ────────────────────────────────────────────────────────── */

static void
ooze_plugin_start (MetaPlugin *plugin)
{
  OozePlugin *self = OOZE_PLUGIN (plugin);
  MetaDisplay *display;
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage;

  g_print ("OozePlugin: compositor plugin started successfully\n");

  ooze_appmenu_setup_environment ();
  /* Registrar activation happens asynchronously in OozeGlobalMenu. */

  /* WAYLAND_DISPLAY is live: publish it and kick the portal services. */
  ooze_portal_env_publish ();

  ooze_autostart_run ();

  /* Xwayland / MetaX11Display often arrives after plugin start. */
  ooze_plugin_schedule_xsettings (self);

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

  ooze_lock_init (self);
  ooze_polkit_init (self);
  ooze_foreign_gel_init (self);
  self->notifications = ooze_notifications_new (self);
  ooze_notifications_reflow (self->notifications);
  self->shot = ooze_shot_new (self);

  clutter_actor_show (stage);
}

/* ── GObject lifecycle ───────────────────────────────────────────────────── */

void
ooze_plugin_begin_shutdown (OozePlugin *plugin)
{
  MetaDisplay *display;
  GList *windows;
  GList *l;

  if (!plugin || plugin->shutting_down)
    return;

  if (plugin->logout_idle)
    {
      g_source_remove (plugin->logout_idle);
      plugin->logout_idle = 0;
    }
  plugin->shutting_down = TRUE;

  /*
   * Logout calls this while Mutter's backend and Clutter seat are still
   * alive.  Tear down anything that can hold focus, a grab, an actor, or a
   * callback before asking MetaContext to dispose the backend.
   */
  ooze_lock_dispose (plugin);
  ooze_session_dialog_dismiss ();
  ooze_foreign_gel_shutdown (plugin);
  ooze_polkit_shutdown ();
  ooze_shot_free (plugin->shot);
  plugin->shot = NULL;
  ooze_notifications_free (plugin->notifications);
  plugin->notifications = NULL;
  ooze_tray_dispose (plugin);
  ooze_panel_dispose (plugin);
  ooze_plugin_cancel_desktop_interactions (plugin);
  ooze_dock_cancel_interactions (plugin->aqua_dock_icons);
  ooze_plugin_clear_aux_panels (plugin);

  if (plugin->monitors_changed_handler && plugin->monitor_manager)
    {
      g_clear_signal_handler (&plugin->monitors_changed_handler,
                              plugin->monitor_manager);
    }

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (display)
    {
      windows = meta_display_list_all_windows (display);
      for (l = windows; l != NULL; l = l->next)
        {
          MetaWindow *window = l->data;
          MetaWindowActor *actor;

          actor = META_WINDOW_ACTOR (meta_window_get_compositor_private (window));
          if (actor)
            {
              ooze_plugin_cancel_window_idle_ops (actor);
              ooze_magic_lamp_cancel (actor);
            }
        }
      g_list_free (windows);
    }

  if (plugin->workspace_added_handler && display)
    {
      MetaWorkspaceManager *wm =
        meta_display_get_workspace_manager (display);
      if (wm)
        g_clear_signal_handler (&plugin->workspace_added_handler, wm);
    }

  ooze_plugin_cancel_xsettings_retry (plugin);
  if (plugin->x11_display_opened_handler && display)
    g_clear_signal_handler (&plugin->x11_display_opened_handler, display);

  if (plugin->stage_key_handler && display)
    {
      MetaBackend *backend;
      ClutterActor *stage;

      backend = meta_context_get_backend (meta_display_get_context (display));
      stage = backend ? CLUTTER_ACTOR (meta_backend_get_stage (backend)) : NULL;
      if (stage)
        g_clear_signal_handler (&plugin->stage_key_handler, stage);
    }

  ooze_theme_unwatch_will_change (NULL, ooze_plugin_on_theme_will_change, plugin);
  ooze_theme_unwatch (NULL, ooze_plugin_on_theme_changed, plugin);

  if (plugin->chrome_theme_idle)
    {
      g_source_remove (plugin->chrome_theme_idle);
      plugin->chrome_theme_idle = 0;
    }
  if (plugin->dock_reflect_idle)
    {
      g_source_remove (plugin->dock_reflect_idle);
      plugin->dock_reflect_idle = 0;
    }

  g_clear_pointer (&plugin->menu_popup, ooze_aqua_menu_destroy);
  ooze_global_menu_free (plugin->global_menu);
  plugin->global_menu = NULL;

  g_clear_pointer (&plugin->aqua_dock, clutter_actor_destroy);
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->aqua_dock_reflections = NULL;
  g_clear_pointer (&plugin->tile_preview, clutter_actor_destroy);
  g_clear_pointer (&plugin->background_group, clutter_actor_destroy);
}

static void
ooze_plugin_dispose (GObject *object)
{
  OozePlugin *plugin = OOZE_PLUGIN (object);

  ooze_plugin_begin_shutdown (plugin);
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
  plugin->shutting_down = FALSE;
  plugin->background_group = NULL;
  plugin->panel = NULL;
  plugin->menu_icon = NULL;
  plugin->clock_label = NULL;
  plugin->aux_panels = NULL;
  plugin->n_aux_panels = 0;
  plugin->aux_panel_widths = NULL;
  memset (plugin->menu_bar_labels, 0, sizeof plugin->menu_bar_labels);
  plugin->aqua_dock = NULL;
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->aqua_dock_reflections = NULL;
  plugin->tile_preview = NULL;
  plugin->monitor_manager = NULL;
  plugin->context = NULL;
  plugin->menu_popup = NULL;
  plugin->logout_idle = 0;
  plugin->monitors_changed_handler = 0;
  plugin->workspace_added_handler = 0;
  plugin->x11_display_opened_handler = 0;
  plugin->xsettings_retry_id = 0;
  plugin->xsettings_retry_tries = 0;
  plugin->clock_timer = 0;
  plugin->stage_key_handler = 0;
  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;
  plugin->dock_reflect_idle = 0;
  plugin->locked = FALSE;
  plugin->lock_enabled = TRUE;
  plugin->lock_overlay = NULL;
  plugin->lock_card = NULL;
  plugin->lock_clock_label = NULL;
  plugin->lock_user_label = NULL;
  plugin->lock_entry_box = NULL;
  plugin->lock_password = NULL;
  plugin->lock_unlock_btn = NULL;
  plugin->lock_status_label = NULL;
  plugin->lock_grab = NULL;
  plugin->lock_auth_proc = NULL;
  plugin->lock_clock_timer = 0;
  plugin->lock_idle_watch_id = 0;
  plugin->lock_logind_sub_id = 0;
  plugin->lock_logind_conn = NULL;
  plugin->session_settings = NULL;
  plugin->screensaver_settings = NULL;
}
