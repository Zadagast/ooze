#include "my-plugin.h"
#include "my-aqua-draw.h"
#include "my-dock.h"
#include "my-theme.h"
#include "my-window.h"

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
#include <meta/window.h>

#include <glib.h>
#include <time.h>

#define PANEL_HEIGHT          22.0f
#define MENU_ICON_SIZE        14.0f
#define MENU_ICON_MARGIN      8.0f
#define MENU_ITEM_GAP         18.0f
#define MENU_TEXT_LEFT        28.0f

#define AQUA_DOCK_PLATE_H     58.0f
#define AQUA_DOCK_ITEM_SIZE   48.0f
#define AQUA_DOCK_PADDING     14.0f
#define AQUA_DOCK_ITEM_GAP    6.0f
#define AQUA_DOCK_BOTTOM_GAP  8.0f

#define AQUA_TITLEBAR_HEIGHT  22.0f
#define TRAFFIC_LIGHT_SIZE    12.0f
#define TRAFFIC_LIGHT_GAP     7.0f
#define TRAFFIC_LIGHT_MARGIN  8.0f
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
  ClutterActor *menu_items;
  ClutterActor *clock_label;
  ClutterActor *aqua_dock;
  ClutterActor *aqua_dock_plate;
  ClutterActor *aqua_dock_icons;
  MetaMonitorManager *monitor_manager;
  MetaContext *context;
  gulong monitors_changed_handler;
  guint clock_timer;
  gulong stage_key_handler;
  int last_panel_width;
  int last_dock_plate_width;
};

G_DEFINE_TYPE (MyPlugin, my_plugin, META_TYPE_PLUGIN)

static const char *menu_bar_items[] = {
  "Spot",
  "File",
  "Edit",
  "View",
  "Go",
  "Window",
  "Help",
};

static void my_plugin_update_clock (MyPlugin *plugin);
static void my_plugin_update_layout (MyPlugin *plugin, MetaDisplay *display);
static void my_plugin_on_monitors_changed (MetaMonitorManager *monitor_manager,
                                           MyPlugin *plugin);
static void my_plugin_schedule_window_chrome_sync (MetaWindowActor *actor);
static void my_plugin_sync_window_chrome (MetaWindowActor *actor);

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
  g_autoptr (ClutterContent) apple_content = NULL;
  const MyAquaPalette *palette;
  ClutterActor *child;
  gsize menu_i = 0;

  if (!plugin->panel)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  palette = my_theme_get_palette (NULL);

  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;

  if (plugin->menu_icon)
    {
      apple_content = my_aqua_apple_logo_content (plugin->panel,
                                                  (int) MENU_ICON_SIZE);
      if (apple_content)
        my_aqua_actor_set_content (plugin->menu_icon,
                                   g_steal_pointer (&apple_content),
                                   (int) MENU_ICON_SIZE,
                                   (int) MENU_ICON_SIZE);
    }

  for (child = clutter_actor_get_first_child (plugin->panel);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      if (child == plugin->menu_icon || child == plugin->clock_label)
        continue;

      if (menu_i < G_N_ELEMENTS (menu_bar_items))
        {
          my_plugin_set_text_label (child,
                                    "Sans Bold 11",
                                    menu_bar_items[menu_i],
                                    (gfloat) palette->menu_text_r,
                                    (gfloat) palette->menu_text_g,
                                    (gfloat) palette->menu_text_b);
          menu_i++;
        }
    }

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
                                    "Sans 10",
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

static ClutterActor *
my_plugin_create_traffic_light (ClutterActor *ref,
                                gfloat        r,
                                gfloat        g,
                                gfloat        b)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;

  content = my_aqua_traffic_light_content (ref,
                                           (int) TRAFFIC_LIGHT_SIZE,
                                           r, g, b);
  actor = clutter_actor_new ();
  clutter_actor_set_size (actor, TRAFFIC_LIGHT_SIZE, TRAFFIC_LIGHT_SIZE);
  if (content)
    my_aqua_actor_set_content (actor,
                               g_steal_pointer (&content),
                               (int) TRAFFIC_LIGHT_SIZE,
                               (int) TRAFFIC_LIGHT_SIZE);

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
  gboolean csd;

  window = meta_window_actor_get_meta_window (actor);
  window_actor = CLUTTER_ACTOR (actor);

  if (g_object_get_data (G_OBJECT (actor), "my-window-chrome"))
    return;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  compositor = meta_display_get_compositor (display);
  window_group = meta_compositor_get_window_group (compositor);

  csd = my_window_is_client_decorated (window);
  if (csd)
    return;

  chrome = g_new0 (MyWindowChrome, 1);

  {
    ClutterActor *close_btn;
    ClutterActor *minimize_btn;
    ClutterActor *zoom_btn;
    gfloat x;

    chrome->titlebar = clutter_actor_new ();
    clutter_actor_set_reactive (chrome->titlebar, TRUE);
    clutter_actor_hide (chrome->titlebar);

    close_btn = my_plugin_create_traffic_light (window_actor, 1.0f, 0.373f, 0.337f);
    minimize_btn = my_plugin_create_traffic_light (window_actor, 1.0f, 0.737f, 0.180f);
    zoom_btn = my_plugin_create_traffic_light (window_actor, 0.153f, 0.788f, 0.251f);

    x = TRAFFIC_LIGHT_MARGIN;
    clutter_actor_add_child (chrome->titlebar, close_btn);
    clutter_actor_set_position (close_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - TRAFFIC_LIGHT_SIZE) / 2.0f);
    x += TRAFFIC_LIGHT_SIZE + TRAFFIC_LIGHT_GAP;

    clutter_actor_add_child (chrome->titlebar, minimize_btn);
    clutter_actor_set_position (minimize_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - TRAFFIC_LIGHT_SIZE) / 2.0f);
    x += TRAFFIC_LIGHT_SIZE + TRAFFIC_LIGHT_GAP;

    clutter_actor_add_child (chrome->titlebar, zoom_btn);
    clutter_actor_set_position (zoom_btn,
                                x,
                                (AQUA_TITLEBAR_HEIGHT - TRAFFIC_LIGHT_SIZE) / 2.0f);

    chrome->title_label = my_plugin_create_text_label (window_actor,
                                                       "Sans 10",
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
                              "Sans 11",
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

  if (plugin->clock_label)
    {
      clock_width = clutter_actor_get_width (plugin->clock_label);
      clutter_actor_set_position (plugin->clock_label,
                                  (gfloat) width - clock_width - 12.0f,
                                  my_plugin_vertical_center (PANEL_HEIGHT,
                                                             clutter_actor_get_height (plugin->clock_label)));
    }

  my_plugin_update_aqua_dock_layout (plugin, display);
}

static void
my_plugin_setup_panel (MyPlugin       *plugin,
                       MetaDisplay    *display,
                       MetaCompositor *compositor)
{
  ClutterActor *stage;
  ClutterActor *window_group;
  g_autoptr (ClutterContent) apple_content = NULL;
  gsize i;
  gfloat menu_x;

  if (plugin->panel)
    return;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  window_group = meta_compositor_get_window_group (compositor);

  plugin->panel = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->panel, FALSE);

  apple_content = my_aqua_apple_logo_content (plugin->panel,
                                              (int) MENU_ICON_SIZE);
  plugin->menu_icon = clutter_actor_new ();
  clutter_actor_set_size (plugin->menu_icon, MENU_ICON_SIZE, MENU_ICON_SIZE);
  if (apple_content)
    my_aqua_actor_set_content (plugin->menu_icon,
                               g_steal_pointer (&apple_content),
                               (int) MENU_ICON_SIZE,
                               (int) MENU_ICON_SIZE);
  clutter_actor_set_position (plugin->menu_icon,
                              MENU_ICON_MARGIN,
                              (PANEL_HEIGHT - MENU_ICON_SIZE) / 2.0f);
  clutter_actor_add_child (plugin->panel, plugin->menu_icon);

  menu_x = MENU_TEXT_LEFT;
  {
    const MyAquaPalette *palette = my_theme_get_palette (NULL);

    for (i = 0; i < G_N_ELEMENTS (menu_bar_items); i++)
      {
        ClutterActor *label;

        label = my_plugin_create_text_label (plugin->panel,
                                             "Sans Bold 11",
                                             menu_bar_items[i],
                                             (gfloat) palette->menu_text_r,
                                             (gfloat) palette->menu_text_g,
                                             (gfloat) palette->menu_text_b);
        clutter_actor_add_child (plugin->panel, label);
        clutter_actor_set_position (label,
                                    menu_x,
                                    my_plugin_vertical_center (PANEL_HEIGHT,
                                                               clutter_actor_get_height (label)));
        menu_x += clutter_actor_get_width (label) + MENU_ITEM_GAP;
      }

    plugin->clock_label = my_plugin_create_text_label (plugin->panel,
                                                       "Sans 11",
                                                       "Sat Jan  1  12:00 PM",
                                                       (gfloat) palette->menu_text_r,
                                                       (gfloat) palette->menu_text_g,
                                                       (gfloat) palette->menu_text_b);
  }
  clutter_actor_add_child (plugin->panel, plugin->clock_label);
  my_plugin_update_clock (plugin);

  if (!plugin->clock_timer)
    plugin->clock_timer = g_timeout_add_seconds (30, on_clock_tick, plugin);

  clutter_actor_add_child (stage, plugin->panel);
  clutter_actor_set_child_above_sibling (stage, plugin->panel, window_group);
  clutter_actor_show (plugin->panel);

  my_plugin_update_layout (plugin, display);
}

static gboolean
on_spot_placeholder_pressed (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  my_dock_launch_spot ();
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
                            NULL);
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
    }

  clutter_actor_show (plugin->background_group);

  my_plugin_setup_panel (plugin, display, compositor);
  my_plugin_setup_aqua_dock (plugin, display, compositor);
  my_plugin_update_layout (plugin, display);
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

  my_theme_get_default ();
  my_theme_watch (NULL, my_plugin_on_theme_changed, plugin);
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
                    MyPlugin    *plugin G_GNUC_UNUSED)
{
  ClutterModifierType state;

  state = clutter_event_get_state (event);
  if ((state & CLUTTER_SUPER_MASK) &&
      clutter_event_get_key_symbol (event) == CLUTTER_KEY_e)
    {
      my_dock_launch_spot ();
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
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

  g_clear_object (&plugin->context);
  g_clear_pointer (&plugin->background_group, clutter_actor_destroy);
  g_clear_pointer (&plugin->panel, clutter_actor_destroy);
  plugin->menu_icon = NULL;
  plugin->menu_items = NULL;
  plugin->clock_label = NULL;
  g_clear_pointer (&plugin->aqua_dock, clutter_actor_destroy);
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;

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
}

static void
my_plugin_init (MyPlugin *plugin)
{
  plugin->background_group = NULL;
  plugin->panel = NULL;
  plugin->menu_icon = NULL;
  plugin->menu_items = NULL;
  plugin->clock_label = NULL;
  plugin->aqua_dock = NULL;
  plugin->aqua_dock_plate = NULL;
  plugin->aqua_dock_icons = NULL;
  plugin->monitor_manager = NULL;
  plugin->context = NULL;
  plugin->monitors_changed_handler = 0;
  plugin->clock_timer = 0;
  plugin->stage_key_handler = 0;
  plugin->last_panel_width = 0;
  plugin->last_dock_plate_width = 0;
}
