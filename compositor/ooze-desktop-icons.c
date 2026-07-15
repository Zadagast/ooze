#include "ooze-desktop-icons.h"

#include "ooze-aqua-draw.h"
#include "ooze-dock-shell.h"
#include "ooze-dnd-bridge.h"
#include "ooze-stall.h"
#include "../common/ooze-font.h"

#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-dnd.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#define DESKTOP_ICON_SIZE   48.0f
#define DESKTOP_ICON_GAP    80.0f
#define DESKTOP_ICON_MARGIN 24.0f
#define DESKTOP_TOP_INSET   28.0f
#define DESKTOP_DOUBLE_MS   400
#define DESKTOP_SPRING_MS   700
#define DESKTOP_DRAG_THRESHOLD 6
#define DESKTOP_ENUM_BATCH 32

typedef struct
{
  char *name;
  char *display_name;
  GFileType type;
  gboolean hidden;
} OozeDesktopEntry;

typedef struct
{
  GWeakRef container_ref;
  GFile *directory;
  GFileEnumerator *enumerator;
  GCancellable *cancellable;
  GPtrArray *entries;
  guint generation;
} OozeDesktopEnumeration;

typedef struct
{
  MetaContext *context;
  MetaDisplay *display;
  ClutterActor *container;
  ClutterActor *ref_actor;
  int width;
  int height;
  guint32 last_click_time;
  char *last_click_path;
  guint spring_id;
  char *spring_path;
  GFileMonitor *monitor;
  gulong dnd_pos_handler;
  gulong dnd_leave_handler;
  MetaDnd *dnd;
  int last_dnd_x;
  int last_dnd_y;
  gboolean last_dnd_over_desktop;
  char *last_dnd_folder;
  gboolean dragging;
  gboolean over_spot;
  char *drag_path;
  gboolean drag_is_dir;
  float drag_start_x;
  float drag_start_y;
  float drag_hot_x;
  float drag_hot_y;
  float pointer_x;
  float pointer_y;
  ClutterActor *drag_ghost;
  ClutterActor *drop_hint;
  ClutterActor *drop_hint_icon; /* non-owning; icon under folder highlight */
  ClutterGrab *grab;
  gboolean shutting_down;
  GHashTable *layout; /* path -> graphene_point_t* */
  gulong dnd_enter_handler;
  guint rebuild_idle;
  GCancellable *enumeration_cancellable;
  guint enumeration_generation;
} OozeDesktopIconData;

#define DESKTOP_REBUILD_DEBOUNCE_MS 200

static void ooze_desktop_icons_rebuild_async (OozeDesktopIconData *data);
static void ooze_desktop_icons_schedule_rebuild (OozeDesktopIconData *data);
static void ooze_desktop_layout_save (OozeDesktopIconData *data);
static gboolean ooze_desktop_spring_fire (gpointer user_data);
static void ooze_desktop_enumerate_children_cb (GObject      *source,
                                                GAsyncResult *result,
                                                gpointer      user_data);
static void ooze_desktop_enumerate_next_cb (GObject      *source,
                                            GAsyncResult *result,
                                            gpointer      user_data);
static void ooze_desktop_enumeration_close_cb (GObject      *source,
                                               GAsyncResult *result,
                                               gpointer      user_data);
static void on_desktop_changed (GFileMonitor     *monitor,
                                GFile            *file,
                                GFile            *other,
                                GFileMonitorEvent event,
                                gpointer          user_data);

static char *
ooze_desktop_root_path (void)
{
  const char *dir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
  if (dir && *dir)
    return g_strdup (dir);
  return g_build_filename (g_get_home_dir (), "Desktop", NULL);
}

static void
ooze_desktop_entry_free (gpointer user_data)
{
  OozeDesktopEntry *entry = user_data;

  if (!entry)
    return;
  g_free (entry->name);
  g_free (entry->display_name);
  g_free (entry);
}

static void
ooze_desktop_enumeration_free (OozeDesktopEnumeration *enumeration)
{
  if (!enumeration)
    return;
  g_weak_ref_clear (&enumeration->container_ref);
  g_clear_object (&enumeration->directory);
  g_clear_object (&enumeration->enumerator);
  g_clear_object (&enumeration->cancellable);
  g_clear_pointer (&enumeration->entries, g_ptr_array_unref);
  g_free (enumeration);
}

static char *
ooze_desktop_layout_file (void)
{
  return g_build_filename (g_get_user_config_dir (), "ooze", "desktop-layout", NULL);
}

static void
ooze_desktop_layout_point_free (gpointer p)
{
  g_free (p);
}

static void
ooze_desktop_layout_load (OozeDesktopIconData *data)
{
  g_autofree char *path = ooze_desktop_layout_file ();
  g_autofree char *contents = NULL;
  g_auto (GStrv) lines = NULL;
  gsize i;

  if (data->layout)
    g_hash_table_remove_all (data->layout);
  else
    data->layout = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                          ooze_desktop_layout_point_free);

  if (!g_file_get_contents (path, &contents, NULL, NULL) || !contents)
    return;

  lines = g_strsplit (contents, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      g_auto (GStrv) parts = NULL;
      graphene_point_t *pt;
      double x, y;

      if (!*lines[i] || lines[i][0] == '#')
        continue;
      parts = g_strsplit (lines[i], "\t", 3);
      if (!parts[0] || !parts[1] || !parts[2])
        continue;
      x = g_ascii_strtod (parts[1], NULL);
      y = g_ascii_strtod (parts[2], NULL);
      pt = g_new (graphene_point_t, 1);
      pt->x = (float) x;
      pt->y = (float) y;
      g_hash_table_insert (data->layout, g_strdup (parts[0]), pt);
    }
}

static void
ooze_desktop_layout_save (OozeDesktopIconData *data)
{
  g_autofree char *path = ooze_desktop_layout_file ();
  g_autofree char *dir = NULL;
  g_autoptr (GString) body = g_string_new ("# path\\tx\\ty\n");
  GHashTableIter iter;
  gpointer key, value;

  if (!data->layout)
    return;

  dir = g_path_get_dirname (path);
  g_mkdir_with_parents (dir, 0700);

  g_hash_table_iter_init (&iter, data->layout);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *p = key;
      graphene_point_t *pt = value;
      g_string_append_printf (body, "%s\t%.0f\t%.0f\n", p, pt->x, pt->y);
    }
  g_file_set_contents (path, body->str, body->len, NULL);
}

static void
ooze_desktop_layout_set (OozeDesktopIconData *data,
                         const char          *path,
                         float                x,
                         float                y)
{
  graphene_point_t *pt;

  if (!data->layout)
    data->layout = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                          ooze_desktop_layout_point_free);
  pt = g_new (graphene_point_t, 1);
  pt->x = x;
  pt->y = y;
  g_hash_table_insert (data->layout, g_strdup (path), pt);
}

static gboolean
ooze_desktop_layout_get (OozeDesktopIconData *data,
                         const char          *path,
                         float               *x,
                         float               *y)
{
  graphene_point_t *pt;

  if (!data->layout || !path)
    return FALSE;
  pt = g_hash_table_lookup (data->layout, path);
  if (!pt)
    return FALSE;
  *x = pt->x;
  *y = pt->y;
  return TRUE;
}

static void
ooze_desktop_snap_grid (float *x, float *y, int width, int height)
{
  int cols, rows;
  int col, row;
  float max_x, max_y;

  cols = MAX (1, (int) (((float) width - DESKTOP_ICON_MARGIN * 2.0f) / DESKTOP_ICON_GAP));
  rows = MAX (1, (int) (((float) height - DESKTOP_TOP_INSET - DESKTOP_ICON_MARGIN) / DESKTOP_ICON_GAP));
  col = (int) roundf ((*x - DESKTOP_ICON_MARGIN) / DESKTOP_ICON_GAP);
  row = (int) roundf ((*y - DESKTOP_TOP_INSET) / DESKTOP_ICON_GAP);
  col = CLAMP (col, 0, cols - 1);
  row = CLAMP (row, 0, rows - 1);
  *x = DESKTOP_ICON_MARGIN + (float) col * DESKTOP_ICON_GAP;
  *y = DESKTOP_TOP_INSET + (float) row * DESKTOP_ICON_GAP;
  max_x = (float) width - DESKTOP_ICON_SIZE - DESKTOP_ICON_MARGIN;
  max_y = (float) height - DESKTOP_ICON_SIZE - 24.0f - DESKTOP_ICON_MARGIN;
  *x = CLAMP (*x, DESKTOP_ICON_MARGIN, max_x);
  *y = CLAMP (*y, DESKTOP_TOP_INSET, max_y);
}

static gboolean
ooze_desktop_cell_taken (OozeDesktopIconData *data,
                         float                x,
                         float                y,
                         const char          *except_path)
{
  ClutterActor *child;

  for (child = clutter_actor_get_first_child (data->container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      const char *p;
      gfloat cx, cy;

      if (g_object_get_data (G_OBJECT (child), "desktop-ghost"))
        continue;
      if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), "desktop-place")))
        continue;
      p = g_object_get_data (G_OBJECT (child), "desktop-path");
      if (except_path && g_strcmp0 (p, except_path) == 0)
        continue;
      cx = clutter_actor_get_x (child);
      cy = clutter_actor_get_y (child);
      if (fabsf (cx - x) < 1.0f && fabsf (cy - y) < 1.0f)
        return TRUE;
    }
  return FALSE;
}

static void
ooze_desktop_find_free_cell (OozeDesktopIconData *data,
                             float               *x,
                             float               *y,
                             const char          *except_path)
{
  int col, row;
  int cols = MAX (1, (int) (((float) data->width - DESKTOP_ICON_MARGIN * 2.0f) / DESKTOP_ICON_GAP));
  int rows = MAX (1, (int) (((float) data->height - DESKTOP_TOP_INSET - DESKTOP_ICON_MARGIN) / DESKTOP_ICON_GAP));

  ooze_desktop_snap_grid (x, y, data->width, data->height);
  if (!ooze_desktop_cell_taken (data, *x, *y, except_path))
    return;

  for (col = 0; col < cols; col++)
    for (row = 0; row < rows; row++)
      {
        float cx = DESKTOP_ICON_MARGIN + (float) col * DESKTOP_ICON_GAP;
        float cy = DESKTOP_TOP_INSET + (float) row * DESKTOP_ICON_GAP;
        if (!ooze_desktop_cell_taken (data, cx, cy, except_path))
          {
            *x = cx;
            *y = cy;
            return;
          }
      }
}

static void
ooze_desktop_spring_cancel (OozeDesktopIconData *data)
{
  if (data->spring_id)
    {
      g_source_remove (data->spring_id);
      data->spring_id = 0;
    }
  g_clear_pointer (&data->spring_path, g_free);
}

static void
ooze_desktop_dismiss_grab (OozeDesktopIconData *data)
{
  if (!data)
    return;
  if (data->grab)
    {
      clutter_grab_dismiss (data->grab);
      data->grab = NULL;
    }
}

static void
ooze_desktop_destroy_ghost (OozeDesktopIconData *data)
{
  if (data->drag_ghost)
    {
      clutter_actor_destroy (data->drag_ghost);
      data->drag_ghost = NULL;
    }
}

static void
ooze_desktop_drop_hint_ensure (OozeDesktopIconData *data)
{
  CoglColor color;

  if (data->drop_hint)
    return;

  data->drop_hint = clutter_actor_new ();
  g_object_set_data (G_OBJECT (data->drop_hint), "desktop-drop-hint",
                     GINT_TO_POINTER (1));
  clutter_actor_set_reactive (data->drop_hint, FALSE);
  /* Spot blue #2968c8 */
  cogl_color_init_from_4f (&color, 0.16f, 0.41f, 0.78f, 0.22f);
  clutter_actor_set_background_color (data->drop_hint, &color);
  clutter_actor_add_child (data->container, data->drop_hint);
  clutter_actor_hide (data->drop_hint);
}

static void
ooze_desktop_drop_hint_hide (OozeDesktopIconData *data)
{
  data->drop_hint_icon = NULL;
  if (data->drop_hint)
    clutter_actor_hide (data->drop_hint);
}

static void
ooze_desktop_drop_hint_show_desktop (OozeDesktopIconData *data)
{
  CoglColor color;

  ooze_desktop_drop_hint_ensure (data);
  data->drop_hint_icon = NULL;
  cogl_color_init_from_4f (&color, 0.16f, 0.41f, 0.78f, 0.14f);
  clutter_actor_set_background_color (data->drop_hint, &color);
  clutter_actor_set_position (data->drop_hint, 8.0f, 8.0f);
  clutter_actor_set_size (data->drop_hint,
                          (gfloat) data->width - 16.0f,
                          (gfloat) data->height - 16.0f);
  clutter_actor_set_child_below_sibling (data->container, data->drop_hint, NULL);
  clutter_actor_show (data->drop_hint);
}

static void
ooze_desktop_drop_hint_show_icon (OozeDesktopIconData *data, ClutterActor *icon)
{
  CoglColor color;
  const float pad = 6.0f;

  if (!icon)
    {
      ooze_desktop_drop_hint_show_desktop (data);
      return;
    }

  ooze_desktop_drop_hint_ensure (data);
  data->drop_hint_icon = icon;
  cogl_color_init_from_4f (&color, 0.16f, 0.41f, 0.78f, 0.35f);
  clutter_actor_set_background_color (data->drop_hint, &color);
  clutter_actor_set_position (data->drop_hint,
                              clutter_actor_get_x (icon) - pad,
                              clutter_actor_get_y (icon) - pad);
  clutter_actor_set_size (data->drop_hint,
                          clutter_actor_get_width (icon) + pad * 2.0f,
                          clutter_actor_get_height (icon) + pad * 2.0f);
  clutter_actor_set_child_below_sibling (data->container, data->drop_hint, icon);
  clutter_actor_show (data->drop_hint);
}

static gboolean
ooze_desktop_path_is_descendant (const char *path, const char *ancestor)
{
  g_autofree char *prefix = NULL;

  if (!path || !ancestor)
    return FALSE;
  if (g_strcmp0 (path, ancestor) == 0)
    return TRUE;
  prefix = g_strconcat (ancestor, G_DIR_SEPARATOR_S, NULL);
  return g_str_has_prefix (path, prefix);
}

static void
ooze_desktop_remonitor (OozeDesktopIconData *data)
{
  g_autofree char *dir_path = ooze_desktop_root_path ();
  g_autoptr (GFile) dir = NULL;

  g_clear_object (&data->monitor);
  g_mkdir_with_parents (dir_path, 0700);
  dir = g_file_new_for_path (dir_path);
  data->monitor = g_file_monitor_directory (dir, G_FILE_MONITOR_NONE, NULL, NULL);
  if (data->monitor)
    g_signal_connect (data->monitor, "changed",
                      G_CALLBACK (on_desktop_changed), data);
}

static gboolean
ooze_desktop_spring_fire (gpointer user_data)
{
  OozeDesktopIconData *data = user_data;
  g_autofree char *path = NULL;

  data->spring_id = 0;
  if (data->shutting_down)
    return G_SOURCE_REMOVE;
  path = g_steal_pointer (&data->spring_path);
  if (!path || !data->context)
    return G_SOURCE_REMOVE;

  /* Folders always open in Spot (keep Clutter drag alive for shell-drag). */
  ooze_dock_launch_spot_path (data->context, path);
  return G_SOURCE_REMOVE;
}

static void
ooze_desktop_spring_arm (OozeDesktopIconData *data, const char *path)
{
  if (!data || !path)
    return;
  if (g_strcmp0 (path, "/") == 0)
    return;
  if (data->drag_path &&
      (g_strcmp0 (path, data->drag_path) == 0 ||
       ooze_desktop_path_is_descendant (path, data->drag_path)))
    return;
  if (data->spring_path && g_strcmp0 (data->spring_path, path) == 0)
    return;
  ooze_desktop_spring_cancel (data);
  data->spring_path = g_strdup (path);
  data->spring_id = g_timeout_add (DESKTOP_SPRING_MS, ooze_desktop_spring_fire, data);
}

static void
ooze_desktop_icon_data_free (gpointer user_data)
{
  OozeDesktopIconData *data = user_data;

  data->shutting_down = TRUE;
  if (data->rebuild_idle)
    {
      g_source_remove (data->rebuild_idle);
      data->rebuild_idle = 0;
    }
  if (data->enumeration_cancellable)
    {
      g_cancellable_cancel (data->enumeration_cancellable);
      g_clear_object (&data->enumeration_cancellable);
    }
  ooze_desktop_spring_cancel (data);
  ooze_desktop_dismiss_grab (data);
  ooze_desktop_destroy_ghost (data);
  ooze_desktop_drop_hint_hide (data);
  if (data->dnd)
    {
      if (data->dnd_enter_handler)
        g_signal_handler_disconnect (data->dnd, data->dnd_enter_handler);
      if (data->dnd_pos_handler)
        g_signal_handler_disconnect (data->dnd, data->dnd_pos_handler);
      if (data->dnd_leave_handler)
        g_signal_handler_disconnect (data->dnd, data->dnd_leave_handler);
    }
  if (data->drop_hint)
    {
      clutter_actor_destroy (data->drop_hint);
      data->drop_hint = NULL;
    }
  g_clear_object (&data->monitor);
  g_clear_pointer (&data->layout, g_hash_table_unref);
  g_free (data->last_click_path);
  g_free (data->last_dnd_folder);
  g_free (data->drag_path);
  g_free (data);
}

void
ooze_desktop_icons_begin_shutdown (ClutterActor *container)
{
  OozeDesktopIconData *data;

  if (!container)
    return;

  data = g_object_get_data (G_OBJECT (container), "desktop-icon-data");
  if (!data || data->shutting_down)
    return;

  data->shutting_down = TRUE;
  if (data->rebuild_idle)
    {
      g_source_remove (data->rebuild_idle);
      data->rebuild_idle = 0;
    }
  if (data->enumeration_cancellable)
    g_cancellable_cancel (data->enumeration_cancellable);
  ooze_desktop_spring_cancel (data);
  ooze_desktop_dismiss_grab (data);
  ooze_desktop_destroy_ghost (data);
  ooze_desktop_drop_hint_hide (data);
  if (data->dnd)
    {
      if (data->dnd_enter_handler)
        g_clear_signal_handler (&data->dnd_enter_handler, data->dnd);
      if (data->dnd_pos_handler)
        g_clear_signal_handler (&data->dnd_pos_handler, data->dnd);
      if (data->dnd_leave_handler)
        g_clear_signal_handler (&data->dnd_leave_handler, data->dnd);
      data->dnd = NULL;
    }
}

static void
ooze_desktop_spot_action (const char *action_name, GVariant *param)
{
  g_autoptr (GDBusConnection) bus = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder av;
  GVariantBuilder platform;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus)
    return;

  g_variant_builder_init (&av, G_VARIANT_TYPE ("av"));
  if (param)
    g_variant_builder_add_value (&av, g_variant_new_variant (param));
  g_variant_builder_init (&platform, G_VARIANT_TYPE ("a{sv}"));

  g_dbus_connection_call (bus,
                          "org.ooze.Spot",
                          "/org/ooze/Spot",
                          "org.freedesktop.Application",
                          "ActivateAction",
                          g_variant_new ("(sava{sv})",
                                         action_name,
                                         &av,
                                         &platform),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          NULL,
                          NULL);
}

static void
ooze_desktop_spot_receive (const char * const *paths, gboolean prefer_move)
{
  if (!paths || !paths[0])
    return;
  ooze_desktop_spot_action ("receive-files",
                            g_variant_new ("(^asb)", paths, prefer_move));
}

static void
ooze_desktop_spot_drag_motion (double local_x, double local_y)
{
  ooze_desktop_spot_action ("shell-drag-motion",
                            g_variant_new ("(dd)", local_x, local_y));
}

static void
ooze_desktop_spot_drag_leave (void)
{
  ooze_desktop_spot_action ("shell-drag-leave", NULL);
}

static gboolean
ooze_desktop_window_is_spot (MetaWindow *w)
{
  const char *app_id;

  if (!w || meta_window_is_hidden (w))
    return FALSE;
  app_id = meta_window_get_gtk_application_id (w);
  if (g_strcmp0 (app_id, "org.ooze.Spot") == 0)
    return TRUE;
  app_id = meta_window_get_sandboxed_app_id (w);
  return g_strcmp0 (app_id, "org.ooze.Spot") == 0;
}

static MetaWindow *
ooze_desktop_spot_window_at (MetaDisplay *display, int x, int y)
{
  GList *all;
  GList *gl;
  GSList *input = NULL;
  GSList *sorted;
  GSList *l;
  MetaWindow *found = NULL;

  all = meta_display_list_all_windows (display);
  for (gl = all; gl != NULL; gl = gl->next)
    input = g_slist_prepend (input, gl->data);
  g_list_free (all);

  sorted = meta_display_sort_windows_by_stacking (display, input);
  g_slist_free (input);

  /* Bottom → top; reverse so first hit is topmost. */
  sorted = g_slist_reverse (sorted);
  for (l = sorted; l != NULL; l = l->next)
    {
      MetaWindow *w = l->data;
      MtkRectangle rect;

      if (!ooze_desktop_window_is_spot (w))
        continue;
      meta_window_get_frame_rect (w, &rect);
      if (x >= rect.x && x < rect.x + rect.width &&
          y >= rect.y && y < rect.y + rect.height)
        {
          found = w;
          break;
        }
    }
  g_slist_free (sorted);
  return found;
}

static void
ooze_desktop_update_spot_hover (OozeDesktopIconData *data, int x, int y)
{
  MetaWindow *spot = ooze_desktop_spot_window_at (data->display, x, y);

  if (spot)
    {
      MtkRectangle rect;
      meta_window_get_frame_rect (spot, &rect);
      if (!data->over_spot)
        {
          meta_window_activate (spot, 0);
          data->over_spot = TRUE;
        }
      ooze_desktop_spot_drag_motion ((double) (x - rect.x),
                                     (double) (y - rect.y));
    }
  else if (data->over_spot)
    {
      ooze_desktop_spot_drag_leave ();
      data->over_spot = FALSE;
    }
}

static void
ooze_desktop_icon_launch (OozeDesktopIconData *data, const char *path)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileInfo) info = NULL;

  if (!data || !data->context || !path || !*path)
    return;

  file = g_file_new_for_path (path);
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
      ooze_dock_launch_spot_path (data->context, path);
      return;
    }

  if (info && g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR)
    {
      g_autofree char *parent = g_path_get_dirname (path);
      ooze_dock_launch_spot_path (data->context, parent);
      return;
    }

  ooze_dock_launch_spot_path (data->context, path);
}

static ClutterActor *
ooze_desktop_icon_at_xy (OozeDesktopIconData *data, gfloat stage_x, gfloat stage_y)
{
  ClutterActor *child;

  for (child = clutter_actor_get_first_child (data->container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      gfloat lx, ly;

      if (g_object_get_data (G_OBJECT (child), "desktop-ghost"))
        continue;

      if (!clutter_actor_transform_stage_point (child, stage_x, stage_y, &lx, &ly))
        continue;
      if (lx >= 0.0f && ly >= 0.0f &&
          lx <= clutter_actor_get_width (child) &&
          ly <= clutter_actor_get_height (child))
        return child;
    }
  return NULL;
}

static ClutterActor *ooze_desktop_icon_create (ClutterActor *ref_actor,
                                               MetaDisplay *display,
                                               OozeDesktopIconData *data,
                                               const char *label,
                                               const char *path,
                                               const char * const *icon_names,
                                               gboolean is_dir,
                                               gboolean is_place,
                                               gfloat r, gfloat g, gfloat b);

static void
ooze_desktop_ensure_ghost (OozeDesktopIconData *data,
                           ClutterActor        *source,
                           const char          *path,
                           gboolean             is_dir)
{
  ClutterActor *stage;
  static const char *folder_icons[] = { "folder", NULL };
  static const char *file_icons[] = { "text-x-generic", "application-x-generic", NULL };
  g_autofree char *label = NULL;

  if (data->drag_ghost)
    return;

  label = g_path_get_basename (path);
  data->drag_ghost = ooze_desktop_icon_create (data->ref_actor, data->display, data,
                                               label, path,
                                               is_dir ? folder_icons : file_icons,
                                               is_dir, FALSE,
                                               is_dir ? 0.95f : 0.75f,
                                               is_dir ? 0.80f : 0.75f,
                                               is_dir ? 0.35f : 0.78f);
  g_object_set_data (G_OBJECT (data->drag_ghost), "desktop-ghost",
                     GINT_TO_POINTER (1));
  clutter_actor_set_opacity (data->drag_ghost, 200);
  clutter_actor_set_position (data->drag_ghost,
                              clutter_actor_get_x (source),
                              clutter_actor_get_y (source));
  clutter_actor_add_child (data->container, data->drag_ghost);
  clutter_actor_set_child_above_sibling (data->container, data->drag_ghost, NULL);
  clutter_actor_show (data->drag_ghost);

  clutter_actor_set_opacity (source, 80);

  ooze_desktop_dismiss_grab (data);
  stage = clutter_actor_get_stage (data->drag_ghost);
  if (stage)
    data->grab = clutter_stage_grab (CLUTTER_STAGE (stage), data->drag_ghost);
}

static gboolean
ooze_desktop_icon_pressed (ClutterActor *actor,
                           ClutterEvent *event,
                           OozeDesktopIconData *data)
{
  const char *path;
  guint32 time;
  guint interval;
  gboolean is_dir;
  gfloat ax, ay;
  ClutterActor *stage;
  gfloat local_x, local_y;

  if (data->shutting_down)
    return CLUTTER_EVENT_STOP;
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;
  if (g_object_get_data (G_OBJECT (actor), "desktop-ghost"))
    return CLUTTER_EVENT_STOP;

  path = g_object_get_data (G_OBJECT (actor), "desktop-path");
  is_dir = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "desktop-is-dir"));
  if (!path)
    return CLUTTER_EVENT_PROPAGATE;

  clutter_event_get_coords (event, &ax, &ay);
  data->pointer_x = ax;
  data->pointer_y = ay;
  ooze_desktop_dismiss_grab (data);
  ooze_desktop_destroy_ghost (data);

  data->dragging = FALSE;
  g_free (data->drag_path);
  data->drag_path = g_strdup (path);
  data->drag_is_dir = is_dir;
  data->drag_start_x = ax;
  data->drag_start_y = ay;
  data->drag_hot_x = 0.0f;
  data->drag_hot_y = 0.0f;
  if (clutter_actor_transform_stage_point (actor, ax, ay, &local_x, &local_y))
    {
      data->drag_hot_x = local_x;
      data->drag_hot_y = local_y;
    }

  stage = clutter_actor_get_stage (actor);
  if (stage)
    data->grab = clutter_stage_grab (CLUTTER_STAGE (stage), actor);

  time = clutter_event_get_time (event);
  interval = time - data->last_click_time;

  if (data->last_click_path &&
      g_strcmp0 (data->last_click_path, path) == 0 &&
      interval < DESKTOP_DOUBLE_MS)
    {
      ooze_desktop_spring_cancel (data);
      ooze_desktop_dismiss_grab (data);
      g_clear_pointer (&data->drag_path, g_free);
      ooze_desktop_icon_launch (data, path);
      g_clear_pointer (&data->last_click_path, g_free);
      data->last_click_time = 0;
      return CLUTTER_EVENT_STOP;
    }

  g_free (data->last_click_path);
  data->last_click_path = g_strdup (path);
  data->last_click_time = time;
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_desktop_icon_motion (ClutterActor *actor,
                          ClutterEvent *event,
                          OozeDesktopIconData *data)
{
  gfloat x, y;
  float dx, dy;
  ClutterActor *follow;

  if (data->shutting_down)
    return CLUTTER_EVENT_STOP;
  if (!data->drag_path)
    return CLUTTER_EVENT_PROPAGATE;

  /* Accept motion on the source or the surviving ghost. */
  if (actor != data->drag_ghost &&
      !g_object_get_data (G_OBJECT (actor), "desktop-ghost") &&
      data->drag_ghost == NULL &&
      g_strcmp0 (g_object_get_data (G_OBJECT (actor), "desktop-path"),
                 data->drag_path) != 0)
    return CLUTTER_EVENT_PROPAGATE;

  clutter_event_get_coords (event, &x, &y);
  data->pointer_x = x;
  data->pointer_y = y;
  dx = x - data->drag_start_x;
  dy = y - data->drag_start_y;

  if (!data->dragging &&
      (dx * dx + dy * dy) >= (float) (DESKTOP_DRAG_THRESHOLD * DESKTOP_DRAG_THRESHOLD))
    {
      const char *paths[1];

      data->dragging = TRUE;
      ooze_desktop_spring_cancel (data);
      ooze_desktop_ensure_ghost (data, actor, data->drag_path, data->drag_is_dir);
      paths[0] = data->drag_path;
      ooze_dnd_bridge_set_paths (paths, 1, TRUE);
    }

  follow = data->drag_ghost ? data->drag_ghost : actor;
  if (data->dragging && follow)
    {
      ClutterActor *over;
      const char *opath;
      gboolean is_dir;
      gfloat lx, ly;

      if (clutter_actor_transform_stage_point (data->container, x, y, &lx, &ly))
        clutter_actor_set_position (follow,
                                    lx - data->drag_hot_x,
                                    ly - data->drag_hot_y);

      over = ooze_desktop_icon_at_xy (data, x, y);
      if (over)
        {
          opath = g_object_get_data (G_OBJECT (over), "desktop-path");
          is_dir = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (over), "desktop-is-dir"));
          if (is_dir && opath)
            ooze_desktop_spring_arm (data, opath);
          else
            ooze_desktop_spring_cancel (data);
          if (data->over_spot)
            {
              ooze_desktop_spot_drag_leave ();
              data->over_spot = FALSE;
            }
        }
      else
        {
          ooze_desktop_spring_cancel (data);
          ooze_desktop_update_spot_hover (data, (int) x, (int) y);
          if (!data->over_spot)
            {
              g_autofree char *root = ooze_desktop_root_path ();
              ooze_dnd_bridge_set_hover_dir (root);
            }
        }
    }

  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_desktop_icon_released (ClutterActor *actor,
                            ClutterEvent *event,
                            OozeDesktopIconData *data)
{
  gfloat x, y;
  gboolean did_drag;
  g_autofree char *drag_path = NULL;

  if (data->shutting_down)
    return CLUTTER_EVENT_STOP;
  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  clutter_event_get_coords (event, &x, &y);
  data->pointer_x = x;
  data->pointer_y = y;
  did_drag = data->dragging;
  drag_path = g_strdup (data->drag_path);

  ooze_desktop_spring_cancel (data);
  ooze_desktop_dismiss_grab (data);

  if (did_drag && drag_path)
    {
      ClutterActor *over = ooze_desktop_icon_at_xy (data, x, y);
      const char *opath = NULL;
      gboolean is_dir = FALSE;
      g_autofree char *root = ooze_desktop_root_path ();
      gfloat lx = 0, ly = 0;

      if (over)
        {
          opath = g_object_get_data (G_OBJECT (over), "desktop-path");
          is_dir = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (over), "desktop-is-dir"));
        }

      clutter_actor_transform_stage_point (data->container, x, y, &lx, &ly);
      lx -= data->drag_hot_x;
      ly -= data->drag_hot_y;
      ooze_desktop_snap_grid (&lx, &ly, data->width, data->height);

      if (is_dir && opath && g_strcmp0 (opath, drag_path) != 0 &&
          !GPOINTER_TO_INT (g_object_get_data (G_OBJECT (over), "desktop-place")))
        {
          if (data->over_spot)
            {
              ooze_desktop_spot_drag_leave ();
              data->over_spot = FALSE;
            }
          ooze_dnd_bridge_drop_into (opath);
        }
      else if (ooze_desktop_spot_window_at (data->display, (int) x, (int) y))
        {
          const char *paths[2] = { drag_path, NULL };
          ooze_desktop_spot_receive (paths, TRUE);
          ooze_desktop_spot_drag_leave ();
          data->over_spot = FALSE;
          ooze_dnd_bridge_clear ();
        }
      else if (ooze_dock_point_is_downloads (data->display, (int) x, (int) y))
        {
          g_autofree char *downloads = NULL;
          const char *dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
          if (data->over_spot)
            {
              ooze_desktop_spot_drag_leave ();
              data->over_spot = FALSE;
            }
          downloads = dir && *dir ? g_strdup (dir)
                                  : g_build_filename (g_get_home_dir (), "Downloads", NULL);
          ooze_dnd_bridge_drop_into (downloads);
        }
      else
        {
          /* Empty desktop: snap icon to grid. */
          if (data->over_spot)
            {
              ooze_desktop_spot_drag_leave ();
              data->over_spot = FALSE;
            }
          ooze_desktop_find_free_cell (data, &lx, &ly, drag_path);
          ooze_desktop_layout_set (data, drag_path, lx, ly);
          ooze_desktop_layout_save (data);
          ooze_dnd_bridge_clear ();
          (void) root;
        }
    }
  else if (data->over_spot)
    {
      ooze_desktop_spot_drag_leave ();
      data->over_spot = FALSE;
    }

  ooze_desktop_destroy_ghost (data);
  data->dragging = FALSE;
  g_clear_pointer (&data->drag_path, g_free);
  ooze_desktop_icons_rebuild_async (data);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
ooze_desktop_icon_create (ClutterActor *ref_actor,
                          MetaDisplay *display,
                          OozeDesktopIconData *data,
                          const char *label,
                          const char *path,
                          const char * const *icon_names,
                          gboolean is_dir,
                          gboolean is_place,
                          gfloat r, gfloat g, gfloat b)
{
  ClutterActor *icon;
  ClutterActor *label_actor;
  g_autoptr (ClutterContent) icon_content = NULL;
  g_autoptr (ClutterContent) label_content = NULL;
  int label_w = 1;
  int label_h = 1;
  int texture;

  icon = clutter_actor_new ();
  clutter_actor_set_reactive (icon, TRUE);
  clutter_actor_set_size (icon, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE + 18.0f);
  g_object_set_data_full (G_OBJECT (icon), "desktop-path", g_strdup (path), g_free);
  g_object_set_data (G_OBJECT (icon), "desktop-is-dir", GINT_TO_POINTER (is_dir ? 1 : 0));
  g_object_set_data (G_OBJECT (icon), "desktop-place", GINT_TO_POINTER (is_place ? 1 : 0));

  texture = ooze_aqua_icon_texture_size (display, (int) DESKTOP_ICON_SIZE);
  if (icon_names)
    icon_content = ooze_dock_themed_icon_content (ref_actor, display, icon_names,
                                                  (int) DESKTOP_ICON_SIZE);
  if (!icon_content)
    icon_content = ooze_aqua_dock_icon_content (ref_actor, (int) DESKTOP_ICON_SIZE, r, g, b);
  if (icon_content)
    {
      ClutterActor *image = clutter_actor_new ();
      ooze_aqua_actor_set_scaled_content (image, g_steal_pointer (&icon_content),
                                          (int) DESKTOP_ICON_SIZE, (int) DESKTOP_ICON_SIZE,
                                          texture, texture);
      clutter_actor_add_child (icon, image);
      clutter_actor_show (image);
    }

  label_content = ooze_aqua_text_content (ref_actor, OOZE_UI_FONT, label,
                                          1.0f, 1.0f, 1.0f, &label_w, &label_h);
  label_actor = clutter_actor_new ();
  if (label_content)
    ooze_aqua_actor_set_content (label_actor, g_steal_pointer (&label_content),
                                 label_w, label_h);
  clutter_actor_set_position (label_actor,
                              (DESKTOP_ICON_SIZE - label_w) / 2.0f,
                              DESKTOP_ICON_SIZE + 2.0f);
  clutter_actor_add_child (icon, label_actor);
  clutter_actor_show (label_actor);

  g_signal_connect (icon, "button-press-event",
                    G_CALLBACK (ooze_desktop_icon_pressed), data);
  g_signal_connect (icon, "motion-event",
                    G_CALLBACK (ooze_desktop_icon_motion), data);
  g_signal_connect (icon, "button-release-event",
                    G_CALLBACK (ooze_desktop_icon_released), data);
  return icon;
}

static void
ooze_desktop_clear_children (OozeDesktopIconData *data)
{
  ClutterActor *child;
  ClutterActor *next;

  child = clutter_actor_get_first_child (data->container);
  while (child)
    {
      next = clutter_actor_get_next_sibling (child);
      if (!g_object_get_data (G_OBJECT (child), "desktop-ghost") &&
          !g_object_get_data (G_OBJECT (child), "desktop-drop-hint"))
        clutter_actor_destroy (child);
      child = next;
    }
}

static void
ooze_desktop_place_at (ClutterActor *container,
                       ClutterActor *icon,
                       float         x,
                       float         y)
{
  clutter_actor_set_position (icon, x, y);
  clutter_actor_add_child (container, icon);
  clutter_actor_show (icon);
}

static void
ooze_desktop_icons_apply (OozeDesktopIconData *data,
                          GPtrArray            *entries)
{
  g_autoptr (OozeStallScope) stall = NULL;
  static const char *hd_icons[] = { "drive-harddisk", NULL };
  static const char *home_icons[] = { "user-home", "go-home", NULL };
  static const char *folder_icons[] = { "folder", NULL };
  static const char *file_icons[] = { "text-x-generic", "application-x-generic", NULL };
  g_autofree char *view_path = NULL;
  g_autofree char *home_label = NULL;
  gfloat right_x, right_y;
  gfloat auto_x, auto_y;
  ClutterActor *icon;
  guint i;

  if (!data || data->shutting_down || !data->container || !entries)
    return;

  stall = ooze_stall_begin ("desktop-rebuild");
  ooze_desktop_clear_children (data);

  view_path = ooze_desktop_root_path ();
  auto_x = DESKTOP_ICON_MARGIN;
  auto_y = DESKTOP_TOP_INSET;

  right_x = (gfloat) data->width - DESKTOP_ICON_SIZE - DESKTOP_ICON_MARGIN;
  right_y = DESKTOP_TOP_INSET;

  icon = ooze_desktop_icon_create (data->ref_actor, data->display, data,
                                   "Linux HD", "/", hd_icons, TRUE, TRUE,
                                   0.85f, 0.22f, 0.18f);
  ooze_desktop_place_at (data->container, icon, right_x, right_y);
  right_y += DESKTOP_ICON_GAP;

  home_label = g_strdup (g_get_user_name ());
  icon = ooze_desktop_icon_create (data->ref_actor, data->display, data,
                                   home_label, g_get_home_dir (), home_icons, TRUE, TRUE,
                                   0.22f, 0.48f, 0.92f);
  ooze_desktop_place_at (data->container, icon, right_x, right_y);

  for (i = 0; i < entries->len; i++)
    {
      OozeDesktopEntry *entry = entries->pdata[i];
      g_autofree char *child_path = NULL;
      gboolean is_dir;
      float x, y;

      if (entry->hidden)
        continue;

      child_path = g_build_filename (view_path, entry->name, NULL);
      if (data->drag_path && g_strcmp0 (child_path, data->drag_path) == 0)
        continue;

      is_dir = entry->type == G_FILE_TYPE_DIRECTORY;
      icon = ooze_desktop_icon_create (data->ref_actor, data->display, data,
                                       entry->display_name, child_path,
                                       is_dir ? folder_icons : file_icons,
                                       is_dir, FALSE,
                                       is_dir ? 0.95f : 0.75f,
                                       is_dir ? 0.80f : 0.75f,
                                       is_dir ? 0.35f : 0.78f);

      if (ooze_desktop_layout_get (data, child_path, &x, &y))
        {
          ooze_desktop_snap_grid (&x, &y, data->width, data->height);
        }
      else
        {
          x = auto_x;
          y = auto_y;
          ooze_desktop_find_free_cell (data, &x, &y, child_path);
          auto_y = y + DESKTOP_ICON_GAP;
          if (auto_y + DESKTOP_ICON_SIZE + 24.0f >
              (gfloat) data->height - DESKTOP_ICON_MARGIN)
            {
              auto_y = DESKTOP_TOP_INSET;
              auto_x += DESKTOP_ICON_GAP;
            }
        }

      ooze_desktop_place_at (data->container, icon, x, y);
    }

  if (data->drag_ghost)
    clutter_actor_set_child_above_sibling (data->container, data->drag_ghost, NULL);

  clutter_actor_set_size (data->container, (gfloat) data->width, (gfloat) data->height);
}

static void
ooze_desktop_enumeration_finish (OozeDesktopEnumeration *enumeration)
{
  ClutterActor *container;
  OozeDesktopIconData *data;

  container = g_weak_ref_get (&enumeration->container_ref);
  if (container)
    {
      data = g_object_get_data (G_OBJECT (container), "desktop-icon-data");
      if (data &&
          data->enumeration_generation == enumeration->generation &&
          data->enumeration_cancellable == enumeration->cancellable)
        {
          g_clear_object (&data->enumeration_cancellable);
          if (!g_cancellable_is_cancelled (enumeration->cancellable) &&
              !data->shutting_down &&
              !data->dragging)
            ooze_desktop_icons_apply (data, enumeration->entries);
        }
    }

  g_clear_object (&container);
  ooze_desktop_enumeration_free (enumeration);
}

static void
ooze_desktop_enumeration_close (OozeDesktopEnumeration *enumeration)
{
  if (enumeration->enumerator &&
      !g_file_enumerator_is_closed (enumeration->enumerator))
    {
      g_file_enumerator_close_async (enumeration->enumerator,
                                     G_PRIORITY_DEFAULT,
                                     enumeration->cancellable,
                                     ooze_desktop_enumeration_close_cb,
                                     enumeration);
      return;
    }

  ooze_desktop_enumeration_finish (enumeration);
}

static void
ooze_desktop_enumerate_next_cb (GObject      *source,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  OozeDesktopEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;
  GList *infos;
  GList *l;
  gsize n_infos;

  infos = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source),
                                               result, &error);
  if (error)
    {
      ooze_desktop_enumeration_close (enumeration);
      return;
    }

  n_infos = g_list_length (infos);
  for (l = infos; l != NULL; l = l->next)
    {
      GFileInfo *info = l->data;
      OozeDesktopEntry *entry = g_new0 (OozeDesktopEntry, 1);

      entry->name = g_strdup (g_file_info_get_name (info));
      entry->display_name = g_strdup (g_file_info_get_display_name (info));
      entry->type = g_file_info_get_file_type (info);
      entry->hidden = g_file_info_get_is_hidden (info);
      g_ptr_array_add (enumeration->entries, entry);
    }
  g_list_free_full (infos, g_object_unref);

  if (n_infos < DESKTOP_ENUM_BATCH ||
      g_file_enumerator_is_closed (enumeration->enumerator))
    {
      ooze_desktop_enumeration_close (enumeration);
      return;
    }

  g_file_enumerator_next_files_async (enumeration->enumerator,
                                      DESKTOP_ENUM_BATCH,
                                      G_PRIORITY_DEFAULT,
                                      enumeration->cancellable,
                                      ooze_desktop_enumerate_next_cb,
                                      enumeration);
}

static void
ooze_desktop_enumerate_children_cb (GObject      *source,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  OozeDesktopEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;

  enumeration->enumerator =
    g_file_enumerate_children_finish (G_FILE (source), result, &error);
  if (error)
    {
      ooze_desktop_enumeration_finish (enumeration);
      return;
    }

  g_file_enumerator_next_files_async (enumeration->enumerator,
                                      DESKTOP_ENUM_BATCH,
                                      G_PRIORITY_DEFAULT,
                                      enumeration->cancellable,
                                      ooze_desktop_enumerate_next_cb,
                                      enumeration);
}

static void
ooze_desktop_enumeration_close_cb (GObject      *source,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  OozeDesktopEnumeration *enumeration = user_data;

  g_file_enumerator_close_finish (G_FILE_ENUMERATOR (source), result, NULL);
  ooze_desktop_enumeration_finish (enumeration);
}

static void
ooze_desktop_icons_rebuild_async (OozeDesktopIconData *data)
{
  OozeDesktopEnumeration *enumeration;
  g_autofree char *view_path = NULL;

  if (!data || data->shutting_down || !data->container || data->dragging)
    return;

  if (data->enumeration_cancellable)
    {
      g_cancellable_cancel (data->enumeration_cancellable);
      g_clear_object (&data->enumeration_cancellable);
    }

  data->enumeration_generation++;
  data->enumeration_cancellable = g_cancellable_new ();

  enumeration = g_new0 (OozeDesktopEnumeration, 1);
  g_weak_ref_init (&enumeration->container_ref, data->container);
  enumeration->cancellable = g_object_ref (data->enumeration_cancellable);
  enumeration->entries = g_ptr_array_new_with_free_func (ooze_desktop_entry_free);
  enumeration->generation = data->enumeration_generation;

  view_path = ooze_desktop_root_path ();
  enumeration->directory = g_file_new_for_path (view_path);
  g_file_enumerate_children_async (enumeration->directory,
                                   G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                   G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                   G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                   G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT,
                                   enumeration->cancellable,
                                   ooze_desktop_enumerate_children_cb,
                                   enumeration);
}

static gboolean
ooze_desktop_icons_rebuild_idle (gpointer user_data)
{
  OozeDesktopIconData *data = user_data;

  data->rebuild_idle = 0;
  if (data->shutting_down || data->dragging)
    return G_SOURCE_REMOVE;
  ooze_desktop_icons_rebuild_async (data);
  return G_SOURCE_REMOVE;
}

static void
ooze_desktop_icons_schedule_rebuild (OozeDesktopIconData *data)
{
  if (!data || data->shutting_down)
    return;
  if (data->dragging)
    return;
  if (data->rebuild_idle)
    g_source_remove (data->rebuild_idle);
  data->rebuild_idle =
    g_timeout_add (DESKTOP_REBUILD_DEBOUNCE_MS,
                   ooze_desktop_icons_rebuild_idle,
                   data);
}

static void
on_desktop_changed (GFileMonitor *monitor G_GNUC_UNUSED,
                    GFile *file G_GNUC_UNUSED,
                    GFile *other G_GNUC_UNUSED,
                    GFileMonitorEvent event G_GNUC_UNUSED,
                    gpointer user_data)
{
  OozeDesktopIconData *data = user_data;

  /* Avoid stomping an active drag; release/spring rebuild instead. */
  if (data->dragging)
    return;
  ooze_desktop_icons_schedule_rebuild (data);
}

static ClutterActor *
ooze_desktop_icon_at_point (OozeDesktopIconData *data, int x, int y)
{
  return ooze_desktop_icon_at_xy (data, (gfloat) x, (gfloat) y);
}

static void
on_dnd_enter (MetaDnd *dnd G_GNUC_UNUSED, OozeDesktopIconData *data)
{
  if (data->shutting_down)
    return;
  data->last_dnd_over_desktop = TRUE;
  ooze_desktop_drop_hint_show_desktop (data);
}

static void
on_dnd_position_change (MetaDnd *dnd G_GNUC_UNUSED,
                        int x,
                        int y,
                        OozeDesktopIconData *data)
{
  if (data->shutting_down)
    return;
  ClutterActor *icon = ooze_desktop_icon_at_point (data, x, y);
  const char *path;
  gboolean is_dir;
  g_autofree char *cur = NULL;

  data->last_dnd_x = x;
  data->last_dnd_y = y;
  data->pointer_x = (float) x;
  data->pointer_y = (float) y;
  data->last_dnd_over_desktop = TRUE;
  g_clear_pointer (&data->last_dnd_folder, g_free);

  if (!icon)
    {
      ooze_desktop_spring_cancel (data);
      ooze_desktop_drop_hint_show_desktop (data);
      cur = ooze_desktop_root_path ();
      ooze_dnd_bridge_set_hover_dir (cur);
      return;
    }

  path = g_object_get_data (G_OBJECT (icon), "desktop-path");
  is_dir = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (icon), "desktop-is-dir"));
  if (is_dir && path &&
      !GPOINTER_TO_INT (g_object_get_data (G_OBJECT (icon), "desktop-place")))
    {
      data->last_dnd_folder = g_strdup (path);
      ooze_desktop_spring_arm (data, path);
      ooze_desktop_drop_hint_show_icon (data, icon);
      ooze_dnd_bridge_set_hover_dir (path);
    }
  else
    {
      ooze_desktop_spring_cancel (data);
      ooze_desktop_drop_hint_show_desktop (data);
      cur = ooze_desktop_root_path ();
      ooze_dnd_bridge_set_hover_dir (cur);
    }
}

static void
on_dnd_leave (MetaDnd *dnd G_GNUC_UNUSED, OozeDesktopIconData *data)
{
  if (data->shutting_down)
    return;
  data->last_dnd_over_desktop = FALSE;
  g_clear_pointer (&data->last_dnd_folder, g_free);
  ooze_desktop_spring_cancel (data);
  ooze_desktop_drop_hint_hide (data);
}

ClutterActor *
ooze_desktop_icons_create (MetaContext  *context,
                           MetaDisplay  *display,
                           ClutterActor *ref_actor,
                           int           monitor G_GNUC_UNUSED,
                           int           width,
                           int           height)
{
  ClutterActor *container;
  OozeDesktopIconData *data;
  MetaBackend *backend;

  container = clutter_actor_new ();
  clutter_actor_set_reactive (container, FALSE);

  data = g_new0 (OozeDesktopIconData, 1);
  data->context = context;
  data->display = display;
  data->container = container;
  data->ref_actor = ref_actor;
  data->width = width;
  data->height = height;
  ooze_desktop_layout_load (data);
  g_object_set_data_full (G_OBJECT (container), "desktop-icon-data",
                          data, ooze_desktop_icon_data_free);

  ooze_desktop_remonitor (data);

  backend = meta_context_get_backend (context);
  if (backend)
    {
      data->dnd = meta_backend_get_dnd (backend);
      if (data->dnd)
        {
          data->dnd_enter_handler =
            g_signal_connect (data->dnd, "dnd-enter",
                              G_CALLBACK (on_dnd_enter), data);
          data->dnd_pos_handler =
            g_signal_connect (data->dnd, "dnd-position-change",
                              G_CALLBACK (on_dnd_position_change), data);
          data->dnd_leave_handler =
            g_signal_connect (data->dnd, "dnd-leave",
                              G_CALLBACK (on_dnd_leave), data);
        }
    }

  ooze_desktop_icons_rebuild_async (data);
  return container;
}
