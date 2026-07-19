#include "spot-window.h"

#include "ooze-dnd-bridge.h"
#include "ooze-shared-appmenu.h"

/* OozeKit — shared surface + button widgets */
#include "ooze-surface.h"
#include "ooze-button.h"
#include "ooze-grid-menu.h"
#include "ooze-icons.h"
#include "ooze-theme.h"
#include "ooze-toolbar.h"
#include "ooze-about.h"
#include "ooze-pinline.h"
#include "ooze-scroll.h"
#include "ooze-popover.h"

#include <adwaita.h>
#include <string.h>

#define SPOT_COLUMN_WIDTH     180
#define SPOT_MIN_COLUMNS       3
#define SPOT_LIST_ICON_SIZE    OOZE_ICON_SIZE_LIST
#define SPOT_GRID_ICON_SIZE    OOZE_ICON_SIZE_GRID
#define SPOT_GRID_CELL_WIDTH   84
#define SPOT_TOOLBAR_ICON_SIZE OOZE_ICON_SIZE_TOOLBAR
#define SPOT_SIDEBAR_ICON_SIZE OOZE_ICON_SIZE_SIDEBAR

static const char * const spot_context_open_icons[] = {
  "document-open-symbolic", "document-open", NULL,
};
static const char * const spot_context_cut_icons[] = {
  "edit-cut-symbolic", "edit-cut", NULL,
};
static const char * const spot_context_copy_icons[] = {
  "edit-copy-symbolic", "edit-copy", NULL,
};
static const char * const spot_context_paste_icons[] = {
  "edit-paste-symbolic", "edit-paste", NULL,
};
static const char * const spot_context_trash_icons[] = {
  "user-trash-symbolic", "user-trash", NULL,
};
static const char * const spot_context_folder_icons[] = {
  "folder-new-symbolic", "folder-new", NULL,
};
static const char * const spot_context_refresh_icons[] = {
  "view-refresh-symbolic", "view-refresh", NULL,
};

typedef enum {
  SPOT_VIEW_GRID    = 0,  /* default */
  SPOT_VIEW_COLUMNS = 1,
} SpotViewMode;

struct _SpotWindow
{
  OozeApplicationWindow parent_instance;
  GtkWidget *toolbar;
  GtkWidget *search_entry;

  /* content stack — switches between view modes */
  GtkWidget *content_stack;

  /* column view */
  GtkWidget *columns_scrolled;
  GtkWidget *columns_box;

  /* grid / icon view */
  GtkWidget *grid_scrolled;
  GtkWidget *grid_flow;       /* GtkFlowBox */

  GtkWidget *sidebar;
  GtkWidget *status_label;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *grid_view_button;
  GtkWidget *column_view_button;
  GtkWidget *new_folder_popover;
  GtkWidget *new_folder_entry;
  GtkWidget *context_menu;

  GFile *current_dir;
  GFile *context_target; /* file under right-click / for actions */
  GFile *spring_target;  /* folder under spring-load timer while dragging */
  GFile *shell_drag_dest; /* dest folder while compositor shell-drag is active */
  GtkWidget *shell_drag_highlight; /* widget showing folder drop highlight */
  GList *back_stack;
  GList *forward_stack;
  gpointer grid_enumeration;
  GFile *reveal_target;
  guint last_column_count;
  guint spring_id;
  SpotViewMode view_mode;
  gboolean clipboard_cut;
  gboolean shell_drag_active;
};

G_DEFINE_FINAL_TYPE (SpotWindow, spot_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

typedef struct
{
  const char *label;
  GUserDirectory dir;
  gboolean use_home;
} SpotPlace;

static const SpotPlace sidebar_places[] = {
  { "Home", 0, TRUE },
  { "Desktop", G_USER_DIRECTORY_DESKTOP, FALSE },
  { "Documents", G_USER_DIRECTORY_DOCUMENTS, FALSE },
  { "Downloads", G_USER_DIRECTORY_DOWNLOAD, FALSE },
  { "Pictures", G_USER_DIRECTORY_PICTURES, FALSE },
  { "Music", G_USER_DIRECTORY_MUSIC, FALSE },
  { "Videos", G_USER_DIRECTORY_VIDEOS, FALSE },
};

/*
 * Resolve a sidebar / Go-menu place path. Prefer XDG user-dirs; if that fails
 * (common when the nest remaps XDG_CONFIG_HOME without user-dirs.dirs), fall
 * back to $HOME/<label> so Documents / Downloads / etc. still appear.
 */
static char *
spot_place_path_dup (const SpotPlace *place)
{
  const char *path;

  if (place->use_home)
    return g_strdup (g_get_home_dir ());

  path = g_get_user_special_dir (place->dir);
  if (path && path[0] != '\0')
    return g_strdup (path);

  return g_build_filename (g_get_home_dir (), place->label, NULL);
}

static char *
spot_special_dir_path_dup (GUserDirectory dir,
                           const char    *fallback_name)
{
  const char *path = g_get_user_special_dir (dir);

  if (path && path[0] != '\0')
    return g_strdup (path);

  return g_build_filename (g_get_home_dir (), fallback_name, NULL);
}

/* ══════════════════════════════════════════════════════════════════════════ */

static void
spot_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GtkCssProvider *provider;
  GdkDisplay *display;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider,
                                     /*
                                      * ── Native CSD decoration (Aqua shadow + corner radius) ──
                                      *
                                      * GTK4 wraps every CSD window in a "decoration" node.
                                      * We override it to get the classic Aqua drop-shadow and
                                      * tight 9 px corner radius.
                                      */
                                     "window.csd.spot-finder > decoration {"
                                     "  border-radius: 9px;"
                                     "  box-shadow:"
                                     "    0 2px  6px rgba(0,0,0,0.22),"
                                     "    0 8px 24px rgba(0,0,0,0.40),"
                                     "    0 20px 40px rgba(0,0,0,0.20);"
                                     "}"
                                     "window.csd.spot-finder > decoration:focus {"
                                     "  box-shadow:"
                                     "    0 2px  6px rgba(0,0,0,0.28),"
                                     "    0 10px 30px rgba(0,0,0,0.48),"
                                     "    0 22px 44px rgba(0,0,0,0.22);"
                                     "}"
                                     /* Window body uses Adwaita's adaptive token — white in
                                      * light mode, dark in dark mode, no CSS override needed. */
                                     "window.csd.spot-finder { background: @window_bg_color; }"

                                     /*
                                      * ── Layout / typography only ─────────────────────────────
                                      *
                                      * Backgrounds for toolbar / sidebar / statusbar are drawn by
                                      * OozeSurface (ooze-kit).  Button chrome is drawn by
                                      * OozeButton (ooze-kit).  CSS here is sizing + text only.
                                      */

                                     /* Window content area — adaptive */
                                     ".spot-finder { background: @window_bg_color; }"

                                     /*
                                      * Flush chrome → content join.
                                      * Adwaita/GTK defaults add radius/shadow that make the
                                      * view look like a floating card under the toolbar.
                                      * Square edges so toolbar/sidebar pinlines meet cleanly.
                                      */
                                     ".spot-finder paned,"
                                     ".spot-finder paned > *,"
                                     ".spot-finder stack,"
                                     ".spot-finder scrolledwindow:not(.spot-column),"
                                     ".spot-finder scrolledwindow > viewport,"
                                     ".spot-finder listbox,"
                                     ".spot-finder .spot-grid-scroll,"
                                     ".spot-finder .spot-grid-view,"
                                     ".spot-finder .ooze-surface {"
                                     "  border-radius: 0;"
                                     "  box-shadow: none;"
                                     "  border: none;"
                                     "  margin: 0;"
                                     "  outline: none;"
                                     "}"

                                     /* Column dividers are OozePinline siblings (not CSS shadows). */

                                     /* Search shape only — strip sizing comes from OozeToolbar. */
                                     ".ooze-toolbar .spot-search {"
                                     "  min-width: 120px;"
                                     "  border-radius: 10px;"
                                     "}"

                                     /* ── Sidebar ── */
                                     ".spot-sidebar-list { background: none; }"
                                     ".spot-sidebar-list row { padding: 5px 6px; }"
                                     ".spot-sidebar-list row:hover { background: rgba(128,128,128,0.10); }"
                                     ".spot-sidebar-list row:selected {"
                                     "  background: @accent_bg_color;"
                                     "}"
                                     ".spot-sidebar-list .spot-sidebar-label {"
                                     "  color: @sidebar_fg_color;"
                                     "}"
                                     ".spot-sidebar-list row:selected .spot-sidebar-label {"
                                     "  color: @accent_fg_color;"
                                     "}"

                                     /* ── Column browser ──
                                      * Pane fill only; Aqua pinlines are ooze_pinline_new()
                                      * siblings between columns (box-shadow is clipped). */
                                     ".spot-finder .spot-column {"
                                     "  min-width: 180px;"
                                     "  border-radius: 0;"
                                     "  margin: 0;"
                                     "  border: none;"
                                     "  background: @view_bg_color;"
                                     "  box-shadow: none;"
                                     "}"
                                     ".spot-finder .spot-column > viewport,"
                                     ".spot-finder .spot-column listbox {"
                                     "  background: transparent;"
                                     "  border: none;"
                                     "  box-shadow: none;"
                                     "}"
                                     ".spot-column row { padding: 2px 8px;"
                                     "                   color: @view_fg_color; }"
                                     ".spot-column row:hover { background: alpha(@accent_bg_color, 0.10); }"
                                     ".spot-column row:selected {"
                                     "  background: @accent_bg_color;"
                                     "  color: @accent_fg_color;"
                                     "}"

                                     /* ── Status bar ──
                                      * Surface is edge-flush; OozeKit insets only the label
                                      * (.ooze-surface-statusbar > *) for CSD corner clearance. */
                                     ".spot-statusbar {"
                                     "  background: none;"
                                     "  color: @window_fg_color;"
                                     "  opacity: 0.65;"
                                     "}"

                                     /* Content views share one flat plane under the toolbar. */
                                     ".spot-grid-scroll,"
                                     ".spot-grid-scroll > viewport,"
                                     ".spot-grid-view {"
                                     "  background: @view_bg_color;"
                                     "}"

                                     /* Selection + hover chrome on the FlowBoxChild */
                                     ".spot-grid-view > flowboxchild {"
                                     "  border-radius: 5px;"
                                     "  outline: none;"
                                     "  background: none;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected {"
                                     "  background: @accent_bg_color;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:hover:not(:selected) {"
                                     "  background: alpha(@accent_bg_color, 0.10);"
                                     "}"

                                     /* Filename label — colour follows window fg, not view fg */
                                     ".spot-grid-label {"
                                     "  color: @window_fg_color;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected .spot-grid-label {"
                                     "  color: @accent_fg_color;"
                                     "}"

                                     /* Shell / GTK drop feedback */
                                     ".spot-finder .spot-drop-active {"
                                     "  outline: 2px solid @accent_bg_color;"
                                     "  outline-offset: -2px;"
                                     "  background: alpha(@accent_bg_color, 0.08);"
                                     "}"
                                     ".spot-finder .spot-drop-folder {"
                                     "  outline: 2px solid @accent_bg_color;"
                                     "  outline-offset: -2px;"
                                     "  background: alpha(@accent_bg_color, 0.18);"
                                     "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  loaded = TRUE;
}

static void spot_on_column_row_activated (GtkListBox    *box,
                                         GtkListBoxRow *row,
                                         gpointer       user_data);
static void spot_refresh (SpotWindow *self);
static void spot_on_new_folder_create (GtkButton  *button,
                                       SpotWindow *self);
static void spot_show_error (SpotWindow  *self,
                             const char  *heading,
                             const char  *body);
static void spot_show_new_folder_popover (SpotWindow *self);
static void spot_navigate_to_path_string (SpotWindow *self,
                                          const char *path,
                                          gboolean push_history);
static void spot_navigate_to (SpotWindow *self, GFile *dir, gboolean push_history);
static void spot_open_file (GFile *file);
static void spot_rebuild_columns (SpotWindow *self);
static gint spot_compare_file_info (gconstpointer a, gconstpointer b);
static void spot_update_action_states (SpotWindow *self);
static void spot_install_actions (SpotWindow *self);
static void spot_attach_context_menu (SpotWindow *self, GtkWidget *widget);
static void spot_set_view_mode (SpotWindow *self, SpotViewMode mode);
static void spot_pop_history (SpotWindow *self,
                              GList     **stack,
                              GList     **other_stack);
static void spot_append_menus (SpotWindow *self);

typedef struct
{
  SpotWindow *window;
  GFile *directory;
  GFileEnumerator *enumerator;
  GCancellable *cancellable;
} SpotGridEnumeration;

static void spot_grid_enumeration_free (SpotGridEnumeration *enumeration);
static void spot_grid_enumerate_next_cb (GObject      *source,
                                         GAsyncResult *result,
                                         gpointer      user_data);
static void spot_grid_enumerate_children_cb (GObject      *source,
                                             GAsyncResult *result,
                                             gpointer      user_data);

static void
spot_set_context_target (SpotWindow *self,
                         GFile      *file)
{
  if (self->context_target == file)
    return;
  g_clear_object (&self->context_target);
  if (file)
    self->context_target = g_object_ref (file);
}

static GFile *
spot_find_file_on_widget (GtkWidget *widget)
{
  while (widget)
    {
      GFile *file = g_object_get_data (G_OBJECT (widget), "spot-file");
      if (file)
        return file;
      widget = gtk_widget_get_parent (widget);
    }
  return NULL;
}

static GFile *
spot_pick_file_at (GtkWidget *widget,
                   double     x,
                   double     y)
{
  GtkWidget *picked;

  picked = gtk_widget_pick (widget, x, y, GTK_PICK_DEFAULT);
  return spot_find_file_on_widget (picked);
}

static GFile *
spot_get_selected_file (SpotWindow *self)
{
  if (self->context_target)
    return self->context_target;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_flow)
    {
      g_autoptr (GList) selected = NULL;
      GtkFlowBoxChild *child;
      GtkWidget *cell;

      selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (self->grid_flow));
      if (!selected)
        return NULL;
      child = selected->data;
      cell = gtk_flow_box_child_get_child (child);
      return spot_find_file_on_widget (cell);
    }

  if (self->columns_box)
    {
      GtkWidget *scrolled;

      for (scrolled = gtk_widget_get_last_child (self->columns_box);
           scrolled != NULL;
           scrolled = gtk_widget_get_prev_sibling (scrolled))
        {
          GtkWidget *list;
          GtkListBoxRow *row;

          if (!GTK_IS_SCROLLED_WINDOW (scrolled))
            continue;
          list = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scrolled));
          if (!GTK_IS_LIST_BOX (list))
            continue;
          row = gtk_list_box_get_selected_row (GTK_LIST_BOX (list));
          if (row)
            return spot_find_file_on_widget (GTK_WIDGET (row));
        }
    }

  return NULL;
}

static void
spot_select_file_widget (SpotWindow *self,
                         GtkWidget  *host G_GNUC_UNUSED,
                         GFile      *file)
{
  if (!file)
    return;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_flow)
    {
      GtkWidget *child;

      for (child = gtk_widget_get_first_child (self->grid_flow);
           child != NULL;
           child = gtk_widget_get_next_sibling (child))
        {
          GtkWidget *cell;
          GFile *f;

          if (!GTK_IS_FLOW_BOX_CHILD (child))
            continue;
          cell = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
          f = spot_find_file_on_widget (cell);
          if (f && g_file_equal (f, file))
            {
              gtk_flow_box_select_child (GTK_FLOW_BOX (self->grid_flow),
                                         GTK_FLOW_BOX_CHILD (child));
              return;
            }
        }
    }
  else if (self->columns_box)
    {
      GtkWidget *scrolled;

      for (scrolled = gtk_widget_get_first_child (self->columns_box);
           scrolled != NULL;
           scrolled = gtk_widget_get_next_sibling (scrolled))
        {
          GtkWidget *list;
          GtkWidget *row;

          if (!GTK_IS_SCROLLED_WINDOW (scrolled))
            continue;
          list = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scrolled));
          if (!GTK_IS_LIST_BOX (list))
            continue;

          for (row = gtk_widget_get_first_child (list);
               row != NULL;
               row = gtk_widget_get_next_sibling (row))
            {
              GFile *f;

              if (!GTK_IS_LIST_BOX_ROW (row))
                continue;
              f = spot_find_file_on_widget (row);
              if (f && g_file_equal (f, file))
                {
                  gtk_list_box_select_row (GTK_LIST_BOX (list),
                                           GTK_LIST_BOX_ROW (row));
                  return;
                }
            }
        }
    }
}

static char *
spot_unique_child_name (GFile      *parent,
                        const char *base_name)
{
  g_autofree char *name = NULL;
  g_autofree char *stem = NULL;
  g_autofree char *ext = NULL;
  const char *dot;
  int suffix = 0;

  if (!parent || !base_name || !*base_name)
    return g_strdup ("untitled");

  dot = strrchr (base_name, '.');
  if (dot && dot != base_name)
    {
      stem = g_strndup (base_name, (gsize) (dot - base_name));
      ext = g_strdup (dot);
    }
  else
    {
      stem = g_strdup (base_name);
      ext = g_strdup ("");
    }

  while (TRUE)
    {
      g_autoptr (GFile) child = NULL;

      if (suffix == 0)
        name = g_strdup (base_name);
      else if (suffix == 1)
        name = g_strdup_printf ("%s copy%s", stem, ext);
      else
        name = g_strdup_printf ("%s copy %d%s", stem, suffix, ext);

      child = g_file_get_child (parent, name);
      if (!g_file_query_exists (child, NULL))
        return g_steal_pointer (&name);

      g_clear_pointer (&name, g_free);
      suffix++;
    }
}

static gboolean
spot_copy_file_recursive (GFile   *src,
                          GFile   *dest,
                          GError **error)
{
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  info = g_file_query_info (src,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            error);
  if (!info)
    return FALSE;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    {
      g_autoptr (GFileEnumerator) enumerator = NULL;
      g_autoptr (GError) local_error = NULL;
      GFileInfo *child_info;

      if (!g_file_make_directory (dest, NULL, error))
        return FALSE;

      enumerator = g_file_enumerate_children (src,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL,
                                              error);
      if (!enumerator)
        return FALSE;

      while ((child_info = g_file_enumerator_next_file (enumerator, NULL, &local_error)) != NULL)
        {
          const char *name = g_file_info_get_name (child_info);
          g_autoptr (GFile) child_src = g_file_get_child (src, name);
          g_autoptr (GFile) child_dest = g_file_get_child (dest, name);
          gboolean ok;

          ok = spot_copy_file_recursive (child_src, child_dest, error);
          g_object_unref (child_info);
          if (!ok)
            return FALSE;
        }

      if (local_error)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      return TRUE;
    }

  return g_file_copy (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
}

static void
spot_spring_cancel (SpotWindow *self)
{
  if (self->spring_id)
    {
      g_source_remove (self->spring_id);
      self->spring_id = 0;
    }
  g_clear_object (&self->spring_target);
}

static gboolean
spot_spring_fire (gpointer user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  self->spring_id = 0;
  if (self->spring_target)
    spot_navigate_to (self, self->spring_target, TRUE);
  g_clear_object (&self->spring_target);
  return G_SOURCE_REMOVE;
}

static void
spot_spring_arm (SpotWindow *self, GFile *folder)
{
  if (!self || !folder)
    return;
  if (self->spring_target && g_file_equal (self->spring_target, folder))
    return;
  spot_spring_cancel (self);
  self->spring_target = g_object_ref (folder);
  self->spring_id = g_timeout_add (700, spot_spring_fire, self);
}

static gboolean
spot_file_is_dir (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;

  if (!file)
    return FALSE;
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
  return info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
}

static gboolean
spot_path_is_descendant (GFile *path, GFile *ancestor)
{
  g_autofree char *p = NULL;
  g_autofree char *a = NULL;

  if (!path || !ancestor)
    return FALSE;
  p = g_file_get_path (path);
  a = g_file_get_path (ancestor);
  if (!p || !a)
    return FALSE;
  if (g_strcmp0 (p, a) == 0)
    return TRUE;
  {
    g_autofree char *prefix = g_strconcat (a, G_DIR_SEPARATOR_S, NULL);
    return g_str_has_prefix (p, prefix);
  }
}

typedef enum
{
  SPOT_XFER_AUTO = 0,
  SPOT_XFER_MOVE,
  SPOT_XFER_COPY,
} SpotXferMode;

static gboolean
spot_same_filesystem (GFile *a, GFile *b)
{
  g_autoptr (GFileInfo) ia = NULL;
  g_autoptr (GFileInfo) ib = NULL;
  const char *ida, *idb;

  ia = g_file_query_info (a, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  ib = g_file_query_info (b, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (!ia || !ib)
    return FALSE;
  ida = g_file_info_get_attribute_string (ia, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  idb = g_file_info_get_attribute_string (ib, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  return ida && idb && g_strcmp0 (ida, idb) == 0;
}

static SpotXferMode
spot_xfer_mode_from_drop (GdkDrop *drop)
{
  GdkDragAction actions;

  if (!drop)
    return SPOT_XFER_AUTO;

  actions = gdk_drop_get_actions (drop);
  if ((actions & GDK_ACTION_MOVE) && !(actions & GDK_ACTION_COPY))
    return SPOT_XFER_MOVE;
  if ((actions & GDK_ACTION_COPY) && !(actions & GDK_ACTION_MOVE))
    return SPOT_XFER_COPY;
  return SPOT_XFER_AUTO;
}

static void
spot_transfer_one (GFile       *src,
                   GFile       *dest_dir,
                   SpotXferMode mode)
{
  g_autofree char *basename = NULL;
  g_autofree char *dest_name = NULL;
  g_autoptr (GFile) dest = NULL;
  g_autoptr (GFile) src_parent = NULL;
  g_autoptr (GError) error = NULL;
  gboolean move;

  if (!src || !dest_dir)
    return;
  if (spot_path_is_descendant (dest_dir, src))
    return;

  src_parent = g_file_get_parent (src);
  if (src_parent && g_file_equal (src_parent, dest_dir))
    return;

  basename = g_file_get_basename (src);
  dest_name = spot_unique_child_name (dest_dir, basename);
  dest = g_file_get_child (dest_dir, dest_name);

  if (mode == SPOT_XFER_COPY)
    move = FALSE;
  else if (mode == SPOT_XFER_MOVE)
    move = TRUE;
  else
    move = spot_same_filesystem (src, dest_dir);

  if (move)
    {
      if (!g_file_move (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
        {
          g_clear_error (&error);
          if (!spot_copy_file_recursive (src, dest, &error))
            g_warning ("Spot: copy failed: %s", error->message);
        }
    }
  else if (!spot_copy_file_recursive (src, dest, &error))
    {
      g_warning ("Spot: copy failed: %s", error->message);
    }
}

static void
spot_transfer_file_list (SpotWindow  *self,
                         GdkFileList *file_list,
                         GFile       *dest_dir,
                         SpotXferMode mode)
{
  GSList *files;
  GSList *l;

  if (!file_list || !dest_dir)
    return;

  files = gdk_file_list_get_files (file_list);
  for (l = files; l != NULL; l = l->next)
    spot_transfer_one (l->data, dest_dir, mode);

  spot_refresh (self);
}

void
spot_window_receive_paths (SpotWindow         *self,
                           const char * const *paths,
                           gboolean            prefer_move)
{
  SpotXferMode mode = prefer_move ? SPOT_XFER_MOVE : SPOT_XFER_AUTO;
  GFile *dest;
  gsize i;

  if (!self || !paths)
    return;

  dest = self->shell_drag_dest ? self->shell_drag_dest : self->current_dir;
  if (!dest)
    return;

  for (i = 0; paths[i] != NULL; i++)
    {
      g_autoptr (GFile) src = g_file_new_for_path (paths[i]);
      spot_transfer_one (src, dest, mode);
    }
  spot_refresh (self);
}

static void
spot_drop_clear_folder_highlight (SpotWindow *self)
{
  if (self->shell_drag_highlight)
    {
      gtk_widget_remove_css_class (self->shell_drag_highlight, "spot-drop-folder");
      self->shell_drag_highlight = NULL;
    }
}

static void
spot_drop_set_content_active (SpotWindow *self, gboolean active)
{
  GtkWidget *target = NULL;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_scrolled)
    target = self->grid_scrolled;
  else if (self->columns_scrolled)
    target = self->columns_scrolled;
  else if (self->content_stack)
    target = self->content_stack;

  if (!target)
    return;

  if (active)
    gtk_widget_add_css_class (target, "spot-drop-active");
  else
    gtk_widget_remove_css_class (target, "spot-drop-active");
}

static GtkWidget *
spot_widget_with_file (GtkWidget *widget)
{
  for (; widget != NULL; widget = gtk_widget_get_parent (widget))
    {
      if (g_object_get_data (G_OBJECT (widget), "spot-file"))
        return widget;
    }
  return NULL;
}

static void
spot_drop_highlight_folder_widget (SpotWindow *self, GtkWidget *widget)
{
  if (self->shell_drag_highlight == widget)
    return;
  spot_drop_clear_folder_highlight (self);
  if (widget)
    {
      /* Prefer the FlowBoxChild / ListBoxRow chrome for a clean outline. */
      GtkWidget *row = widget;
      while (row &&
             !GTK_IS_FLOW_BOX_CHILD (row) &&
             !GTK_IS_LIST_BOX_ROW (row))
        row = gtk_widget_get_parent (row);
      if (!row)
        row = widget;
      self->shell_drag_highlight = row;
      gtk_widget_add_css_class (row, "spot-drop-folder");
    }
}

void
spot_window_shell_drag_leave (SpotWindow *self)
{
  if (!self)
    return;
  spot_spring_cancel (self);
  spot_drop_clear_folder_highlight (self);
  spot_drop_set_content_active (self, FALSE);
  self->shell_drag_active = FALSE;
  g_clear_object (&self->shell_drag_dest);
}

void
spot_window_shell_drag_motion (SpotWindow *self, double x, double y)
{
  GtkWidget *picked;
  GtkWidget *file_widget;
  GFile *file = NULL;
  gboolean is_dir = FALSE;

  if (!self)
    return;

  self->shell_drag_active = TRUE;
  spot_drop_set_content_active (self, TRUE);

  picked = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT);
  file_widget = spot_widget_with_file (picked);
  if (file_widget)
    {
      file = g_object_get_data (G_OBJECT (file_widget), "spot-file");
      is_dir = file && spot_file_is_dir (file);
    }

  if (is_dir && file)
    {
      spot_drop_highlight_folder_widget (self, file_widget);
      g_set_object (&self->shell_drag_dest, file);
      spot_spring_arm (self, file);
    }
  else
    {
      spot_drop_clear_folder_highlight (self);
      g_set_object (&self->shell_drag_dest, self->current_dir);
      spot_spring_cancel (self);
    }
}

static GdkContentProvider *
spot_drag_prepare (GtkDragSource *source G_GNUC_UNUSED,
                   double         x G_GNUC_UNUSED,
                   double         y G_GNUC_UNUSED,
                   gpointer       user_data)
{
  GtkWidget *widget = user_data;
  GFile *file = g_object_get_data (G_OBJECT (widget), "spot-file");
  GdkFileList *file_list;
  GSList *files = NULL;
  g_autofree char *path = NULL;

  if (!file)
    return NULL;

  path = g_file_get_path (file);
  if (path)
    {
      const char *paths[1] = { path };
      ooze_dnd_bridge_set_paths (paths, 1, TRUE);
    }

  files = g_slist_prepend (files, g_object_ref (file));
  file_list = gdk_file_list_new_from_list (files);
  g_slist_free_full (files, g_object_unref);
  return gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list);
}

static gboolean
spot_bridge_clear_later (gpointer user_data G_GNUC_UNUSED)
{
  ooze_dnd_bridge_clear ();
  return G_SOURCE_REMOVE;
}

static void
spot_drag_end (GtkDragSource *source G_GNUC_UNUSED,
               GdkDrag       *drag G_GNUC_UNUSED,
               gboolean       delete_data,
               gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  g_autofree char *hover = NULL;
  gboolean changed = FALSE;

  /* If a GTK drop target already handled the transfer, bridge is empty. */
  if (!ooze_dnd_bridge_has_pending ())
    {
      ooze_dnd_bridge_clear ();
      if (delete_data)
        spot_refresh (self);
      return;
    }

  hover = ooze_dnd_bridge_get_hover_dir ();
  if (hover)
    {
      ooze_dnd_bridge_drop_into (hover);
      changed = TRUE;
    }
  else
    {
      ooze_dnd_bridge_clear ();
    }

  /* Belt-and-suspenders clear in case drop_into partially failed. */
  g_timeout_add (50, spot_bridge_clear_later, NULL);

  /* Desktop / shell drops move files out from under this view. */
  if (changed || delete_data)
    spot_refresh (self);
}

static gboolean
spot_on_drop (GtkDropTarget *target,
              const GValue  *value,
              double         x G_GNUC_UNUSED,
              double         y G_GNUC_UNUSED,
              gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *dest_dir;
  GdkFileList *list;
  GdkDrop *drop;
  SpotXferMode mode;

  spot_spring_cancel (self);
  ooze_dnd_bridge_clear ();

  dest_dir = g_object_get_data (G_OBJECT (target), "spot-drop-dir");
  if (!dest_dir)
    dest_dir = self->current_dir;
  if (!dest_dir || !G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return FALSE;

  list = g_value_get_boxed (value);
  drop = gtk_drop_target_get_current_drop (target);
  mode = spot_xfer_mode_from_drop (drop);

  spot_transfer_file_list (self, list, dest_dir, mode);
  spot_window_shell_drag_leave (self);
  return TRUE;
}

static GdkDragAction
spot_on_drop_enter (GtkDropTarget *target,
                    double         x G_GNUC_UNUSED,
                    double         y G_GNUC_UNUSED,
                    gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *dest_dir = g_object_get_data (G_OBJECT (target), "spot-drop-dir");
  GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target));

  spot_drop_set_content_active (self, TRUE);
  if (dest_dir && spot_file_is_dir (dest_dir))
    {
      spot_drop_highlight_folder_widget (self, widget);
      g_set_object (&self->shell_drag_dest, dest_dir);
      spot_spring_arm (self, dest_dir);
    }
  else
    {
      spot_drop_clear_folder_highlight (self);
      g_set_object (&self->shell_drag_dest, self->current_dir);
    }
  return GDK_ACTION_COPY | GDK_ACTION_MOVE;
}

static void
spot_on_drop_leave (GtkDropTarget *target G_GNUC_UNUSED,
                    gpointer       user_data)
{
  spot_window_shell_drag_leave (SPOT_WINDOW (user_data));
}

static void
spot_attach_file_drag (GtkWidget *widget, SpotWindow *self, GFile *file G_GNUC_UNUSED)
{
  GtkDragSource *drag;

  drag = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drag, "prepare", G_CALLBACK (spot_drag_prepare), widget);
  g_signal_connect (drag, "drag-end", G_CALLBACK (spot_drag_end), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drag));
}

static void
spot_attach_folder_drop (GtkWidget   *widget,
                         SpotWindow  *self,
                         GFile       *folder)
{
  GtkDropTarget *drop;

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_object_set_data_full (G_OBJECT (drop), "spot-drop-dir",
                          g_object_ref (folder), g_object_unref);
  g_signal_connect (drop, "drop", G_CALLBACK (spot_on_drop), self);
  g_signal_connect (drop, "enter", G_CALLBACK (spot_on_drop_enter), self);
  g_signal_connect (drop, "leave", G_CALLBACK (spot_on_drop_leave), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drop));
}

static void
spot_attach_dir_drop (GtkWidget *widget, SpotWindow *self)
{
  GtkDropTarget *drop;

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drop, "drop", G_CALLBACK (spot_on_drop), self);
  g_signal_connect (drop, "enter", G_CALLBACK (spot_on_drop_enter), self);
  g_signal_connect (drop, "leave", G_CALLBACK (spot_on_drop_leave), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drop));
}

static void
spot_clipboard_set_files (SpotWindow *self,
                          GFile      *file,
                          gboolean    cut)
{
  GdkClipboard *clipboard;
  GdkContentProvider *provider;
  GdkFileList *file_list;
  GSList *files = NULL;

  if (!file)
    return;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));
  files = g_slist_prepend (files, g_object_ref (file));
  file_list = gdk_file_list_new_from_list (files);
  g_slist_free_full (files, g_object_unref);

  provider = gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list);
  gdk_clipboard_set_content (clipboard, provider);
  g_object_unref (provider);
  /* GdkFileList is consumed/copied by the provider path; drop our ref. */
  g_boxed_free (GDK_TYPE_FILE_LIST, file_list);

  self->clipboard_cut = cut;
  spot_update_action_states (self);
}

static void
spot_paste_files_finish (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  const GValue *value = NULL;
  GdkFileList *file_list;
  GSList *files;
  GSList *l;
  gboolean cut = self->clipboard_cut;

  value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source), result, &error);
  if (!value)
    {
      if (error)
        spot_show_error (self, "Paste Failed", error->message);
      return;
    }

  if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return;

  file_list = g_value_get_boxed (value);
  if (!file_list || !self->current_dir)
    return;

  /* transfer container: free the list, not the GFiles */
  files = gdk_file_list_get_files (file_list);
  for (l = files; l != NULL; l = l->next)
    {
      GFile *src = l->data;
      g_autofree char *basename = NULL;
      g_autofree char *dest_name = NULL;
      g_autoptr (GFile) dest = NULL;
      g_autoptr (GError) copy_error = NULL;

      basename = g_file_get_basename (src);
      dest_name = spot_unique_child_name (self->current_dir, basename);
      dest = g_file_get_child (self->current_dir, dest_name);

      if (cut)
        {
          if (!g_file_move (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &copy_error))
            {
              spot_show_error (self, "Move Failed", copy_error->message);
              cut = FALSE;
              break;
            }
        }
      else if (!spot_copy_file_recursive (src, dest, &copy_error))
        {
          spot_show_error (self, "Paste Failed",
                           copy_error ? copy_error->message : "Could not paste.");
          break;
        }
    }
  g_slist_free (files);

  /* value is owned by the clipboard — do not free/unset it */

  if (cut)
    {
      self->clipboard_cut = FALSE;
      gdk_clipboard_set_content (gtk_widget_get_clipboard (GTK_WIDGET (self)), NULL);
    }

  spot_refresh (self);
  spot_update_action_states (self);
}

static void
spot_action_open (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *param G_GNUC_UNUSED,
                  gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = spot_get_selected_file (self);
  g_autoptr (GFileInfo) info = NULL;

  if (!file)
    return;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL, NULL);
  if (info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}

static void
spot_action_new_folder (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant      *param G_GNUC_UNUSED,
                        gpointer       user_data)
{
  spot_show_new_folder_popover (SPOT_WINDOW (user_data));
}

static void
spot_action_copy (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *param G_GNUC_UNUSED,
                  gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = spot_get_selected_file (self);

  spot_clipboard_set_files (self, file, FALSE);
}

static void
spot_action_cut (GSimpleAction *action G_GNUC_UNUSED,
                 GVariant      *param G_GNUC_UNUSED,
                 gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = spot_get_selected_file (self);

  spot_clipboard_set_files (self, file, TRUE);
}

static void
spot_action_paste (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GdkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));

  if (!self->current_dir)
    return;

  gdk_clipboard_read_value_async (clipboard,
                                  GDK_TYPE_FILE_LIST,
                                  G_PRIORITY_DEFAULT,
                                  NULL,
                                  spot_paste_files_finish,
                                  self);
}

static void
spot_action_trash (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = spot_get_selected_file (self);
  g_autoptr (GError) error = NULL;

  if (!file)
    return;

  if (!g_file_trash (file, NULL, &error))
    {
      spot_show_error (self, "Could Not Move to Trash", error->message);
      return;
    }

  spot_set_context_target (self, NULL);
  spot_refresh (self);
  spot_update_action_states (self);
}

static void
spot_action_refresh (GSimpleAction *action G_GNUC_UNUSED,
                     GVariant      *param G_GNUC_UNUSED,
                     gpointer       user_data)
{
  spot_refresh (SPOT_WINDOW (user_data));
}

static void
spot_action_close_window (GSimpleAction *action G_GNUC_UNUSED,
                          GVariant      *param G_GNUC_UNUSED,
                          gpointer       user_data)
{
  gtk_window_close (GTK_WINDOW (user_data));
}

static void
spot_action_view_grid (GSimpleAction *action G_GNUC_UNUSED,
                       GVariant      *param G_GNUC_UNUSED,
                       gpointer       user_data)
{
  spot_set_view_mode (SPOT_WINDOW (user_data), SPOT_VIEW_GRID);
}

static void
spot_action_view_columns (GSimpleAction *action G_GNUC_UNUSED,
                          GVariant      *param G_GNUC_UNUSED,
                          gpointer       user_data)
{
  spot_set_view_mode (SPOT_WINDOW (user_data), SPOT_VIEW_COLUMNS);
}

static void
spot_action_go_back (GSimpleAction *action G_GNUC_UNUSED,
                     GVariant      *param G_GNUC_UNUSED,
                     gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  if (self->back_stack)
    spot_pop_history (self, &self->back_stack, &self->forward_stack);
  spot_refresh (self);
}

static void
spot_action_go_forward (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant      *param G_GNUC_UNUSED,
                        gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  if (self->forward_stack)
    spot_pop_history (self, &self->forward_stack, &self->back_stack);
  spot_refresh (self);
}

static void
spot_action_go_computer (GSimpleAction *action G_GNUC_UNUSED,
                         GVariant      *param G_GNUC_UNUSED,
                         gpointer       user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), "/", TRUE);
}

static void
spot_action_go_home (GSimpleAction *action G_GNUC_UNUSED,
                     GVariant      *param G_GNUC_UNUSED,
                     gpointer       user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), g_get_home_dir (), TRUE);
}

static void
spot_action_go_desktop (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant      *param G_GNUC_UNUSED,
                        gpointer       user_data)
{
  g_autofree char *path = spot_special_dir_path_dup (G_USER_DIRECTORY_DESKTOP,
                                                     "Desktop");

  if (path)
    spot_navigate_to_path_string (SPOT_WINDOW (user_data), path, TRUE);
}

static void
spot_action_go_documents (GSimpleAction *action G_GNUC_UNUSED,
                          GVariant      *param G_GNUC_UNUSED,
                          gpointer       user_data)
{
  g_autofree char *path = spot_special_dir_path_dup (G_USER_DIRECTORY_DOCUMENTS,
                                                     "Documents");

  if (path)
    spot_navigate_to_path_string (SPOT_WINDOW (user_data), path, TRUE);
}

static void
spot_action_go_downloads (GSimpleAction *action G_GNUC_UNUSED,
                          GVariant      *param G_GNUC_UNUSED,
                          gpointer       user_data)
{
  g_autofree char *path = spot_special_dir_path_dup (G_USER_DIRECTORY_DOWNLOAD,
                                                     "Downloads");

  if (path)
    spot_navigate_to_path_string (SPOT_WINDOW (user_data), path, TRUE);
}

static void
spot_launch_pak (void)
{
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *exe = NULL;

  exe = g_find_program_in_path ("ooze-pak");
  if (!exe)
    {
      g_warning ("Spot: ooze-pak not found in PATH");
      return;
    }

  info = g_app_info_create_from_commandline (exe, NULL,
                                             G_APP_INFO_CREATE_NONE,
                                             &error);
  if (!info)
    {
      g_warning ("Spot: failed to create ooze-pak launcher: %s",
                 error ? error->message : "unknown");
      return;
    }

  if (!g_app_info_launch (info, NULL, NULL, &error))
    g_warning ("Spot: failed to launch ooze-pak: %s",
               error ? error->message : "unknown");
}

static void
spot_action_go_applications (GSimpleAction *action G_GNUC_UNUSED,
                             GVariant      *param G_GNUC_UNUSED,
                             gpointer       user_data G_GNUC_UNUSED)
{
  /* Applications opens Ooze Pak’s installed-app grid (Software). */
  spot_launch_pak ();
}

static void
spot_action_new_window (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant      *param G_GNUC_UNUSED,
                        gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  SpotWindow *win;
  g_autofree char *path = NULL;

  if (self->current_dir)
    path = g_file_get_path (self->current_dir);

  win = spot_window_new_for_path (GTK_APPLICATION (app), path);
  gtk_window_present (GTK_WINDOW (win));
}

static void
spot_action_about (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Spot",
                      "system-file-manager",
                      "File manager for Ooze Desktop.",
                      OOZE_VERSION);
}

static void
spot_append_menus (SpotWindow *self)
{
  GMenu *file, *edit, *view, *go, *window, *help;

  file = g_menu_new ();
  g_menu_append (file, "New Window", "win.new-window");
  g_menu_append (file, "New Folder", "win.new-folder");
  g_menu_append (file, "Open", "win.open");
  g_menu_append (file, "Close Window", "win.close-window");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "File", G_MENU_MODEL (file));
  g_object_unref (file);

  edit = g_menu_new ();
  g_menu_append (edit, "Cut", "win.cut");
  g_menu_append (edit, "Copy", "win.copy");
  g_menu_append (edit, "Paste", "win.paste");
  g_menu_append (edit, "Move to Trash", "win.trash");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Edit", G_MENU_MODEL (edit));
  g_object_unref (edit);

  view = g_menu_new ();
  g_menu_append (view, "as Icons", "win.view-grid");
  g_menu_append (view, "as Columns", "win.view-columns");
  g_menu_append (view, "Refresh", "win.refresh");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "View", G_MENU_MODEL (view));
  g_object_unref (view);

  go = g_menu_new ();
  g_menu_append (go, "Back", "win.go-back");
  g_menu_append (go, "Forward", "win.go-forward");
  g_menu_append (go, "Computer", "win.go-computer");
  g_menu_append (go, "Home", "win.go-home");
  g_menu_append (go, "Desktop", "win.go-desktop");
  g_menu_append (go, "Documents", "win.go-documents");
  g_menu_append (go, "Downloads", "win.go-downloads");
  g_menu_append (go, "Applications", "win.go-applications");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Go", G_MENU_MODEL (go));
  g_object_unref (go);

  window = g_menu_new ();
  g_menu_append (window, "Minimize", "win.minimize");
  g_menu_append (window, "Maximize", "win.maximize");
  g_menu_append (window, "New Window", "win.new-window");
  g_menu_append (window, "Close Window", "win.close-window");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Window", G_MENU_MODEL (window));
  g_object_unref (window);

  help = g_menu_new ();
  g_menu_append (help, "About Spot", "win.about");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", G_MENU_MODEL (help));
  g_object_unref (help);
}

static void
spot_update_action_states (SpotWindow *self)
{
  GAction *action;
  GFile *file = spot_get_selected_file (self);
  gboolean has_file = file != NULL;
  gboolean has_dir = self->current_dir != NULL;

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "open");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_file);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "copy");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_file);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "cut");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_file);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "trash");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_file);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "new-folder");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_dir);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "paste");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_dir);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "refresh");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), has_dir);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "go-back");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                                 self->back_stack != NULL);

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "go-forward");
  if (action)
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                                 self->forward_stack != NULL);
}

typedef struct
{
  SpotWindow *window;
  const char *detailed_action;
} SpotContextAction;

static void
spot_context_action_free (gpointer data)
{
  g_free (data);
}

static void
spot_context_action_activate (gpointer user_data)
{
  SpotContextAction *context_action = user_data;

  gtk_widget_activate_action (GTK_WIDGET (context_action->window),
                              context_action->detailed_action, NULL);
}

static void
spot_context_append_action (GtkWidget                  *menu,
                            SpotWindow                 *self,
                            const char                 *label,
                            const char                 *detailed_action,
                            const char * const         *icon_names)
{
  SpotContextAction *context_action;
  OozeGridMenuItem item;
  const char *action = detailed_action;

  context_action = g_new (SpotContextAction, 1);
  context_action->window = self;
  context_action->detailed_action = detailed_action;
  if (g_str_has_prefix (action, "win."))
    action += strlen ("win.");
  item = (OozeGridMenuItem) {
    .icon_names = icon_names,
    .label = label,
    .sensitive = g_action_group_get_action_enabled (
      G_ACTION_GROUP (self), action),
    .activate = spot_context_action_activate,
    .user_data = context_action,
    .user_data_destroy = spot_context_action_free,
  };
  ooze_grid_menu_append_item (menu, &item);
}

static void
spot_show_context_menu (SpotWindow *self,
                        GtkWidget  *relative_to,
                        double      x,
                        double      y,
                        gboolean    on_item)
{
  GdkRectangle rect;
  graphene_point_t local;
  graphene_point_t in_window;

  spot_update_action_states (self);

  if (self->context_menu)
    {
      gtk_widget_unparent (self->context_menu);
      self->context_menu = NULL;
    }

  /* Parent to the window — never to FlowBox/ListBox. Those containers are
   * cleared on refresh; a popover sibling would make remove() loop forever. */
  self->context_menu = ooze_grid_menu_new ();
  gtk_widget_set_parent (self->context_menu, GTK_WIDGET (self));
  gtk_popover_set_position (GTK_POPOVER (self->context_menu), GTK_POS_BOTTOM);

  if (on_item)
    {
      spot_context_append_action (self->context_menu, self,
                                  "Open", "win.open", spot_context_open_icons);
      ooze_grid_menu_append_separator (self->context_menu);
      spot_context_append_action (self->context_menu, self,
                                  "Cut", "win.cut", spot_context_cut_icons);
      spot_context_append_action (self->context_menu, self,
                                  "Copy", "win.copy", spot_context_copy_icons);
      spot_context_append_action (self->context_menu, self,
                                  "Paste", "win.paste", spot_context_paste_icons);
      ooze_grid_menu_append_separator (self->context_menu);
      spot_context_append_action (self->context_menu, self,
                                  "New Folder", "win.new-folder",
                                  spot_context_folder_icons);
      spot_context_append_action (self->context_menu, self,
                                  "Move to Trash", "win.trash",
                                  spot_context_trash_icons);
    }
  else
    {
      spot_context_append_action (self->context_menu, self,
                                  "New Folder", "win.new-folder",
                                  spot_context_folder_icons);
      spot_context_append_action (self->context_menu, self,
                                  "Paste", "win.paste", spot_context_paste_icons);
      spot_context_append_action (self->context_menu, self,
                                  "Refresh", "win.refresh",
                                  spot_context_refresh_icons);
    }

  ooze_popover_fit_screen (GTK_POPOVER (self->context_menu));

  graphene_point_init (&local, (float) x, (float) y);
  if (gtk_widget_compute_point (relative_to, GTK_WIDGET (self), &local, &in_window))
    {
      rect.x = (int) in_window.x;
      rect.y = (int) in_window.y;
    }
  else
    {
      rect.x = (int) x;
      rect.y = (int) y;
    }
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (self->context_menu), &rect);
  gtk_popover_popup (GTK_POPOVER (self->context_menu));
}

static void
on_context_pressed (GtkGestureClick *gesture,
                    gint             n_press,
                    gdouble          x,
                    gdouble          y,
                    SpotWindow      *self)
{
  GtkWidget *widget;
  GFile *file;

  if (n_press != 1)
    return;

  widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  file = spot_pick_file_at (widget, x, y);

  if (file)
    {
      spot_set_context_target (self, file);
      spot_select_file_widget (self, widget, file);
    }
  else
    {
      spot_set_context_target (self, NULL);
    }

  spot_show_context_menu (self, widget, x, y, file != NULL);
}

static void
spot_attach_context_menu (SpotWindow *self,
                          GtkWidget  *widget)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect (gesture, "pressed", G_CALLBACK (on_context_pressed), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}

static void
spot_install_actions (SpotWindow *self)
{
  static const GActionEntry entries[] = {
    { "open",            spot_action_open,            NULL, NULL, NULL },
    { "new-folder",      spot_action_new_folder,      NULL, NULL, NULL },
    { "copy",            spot_action_copy,            NULL, NULL, NULL },
    { "cut",             spot_action_cut,             NULL, NULL, NULL },
    { "paste",           spot_action_paste,           NULL, NULL, NULL },
    { "trash",           spot_action_trash,           NULL, NULL, NULL },
    { "refresh",         spot_action_refresh,         NULL, NULL, NULL },
    { "close-window",    spot_action_close_window,    NULL, NULL, NULL },
    { "view-grid",       spot_action_view_grid,       NULL, NULL, NULL },
    { "view-columns",    spot_action_view_columns,    NULL, NULL, NULL },
    { "go-back",         spot_action_go_back,         NULL, NULL, NULL },
    { "go-forward",      spot_action_go_forward,      NULL, NULL, NULL },
    { "go-computer",     spot_action_go_computer,     NULL, NULL, NULL },
    { "go-home",         spot_action_go_home,         NULL, NULL, NULL },
    { "go-desktop",      spot_action_go_desktop,      NULL, NULL, NULL },
    { "go-documents",    spot_action_go_documents,    NULL, NULL, NULL },
    { "go-downloads",    spot_action_go_downloads,    NULL, NULL, NULL },
    { "go-applications", spot_action_go_applications, NULL, NULL, NULL },
    { "new-window",      spot_action_new_window,      NULL, NULL, NULL },
    { "about",           spot_action_about,           NULL, NULL, NULL },
  };
  GtkApplication *app;
  struct {
    const char *action;
    const char *accel;
  } accels[] = {
    { "win.copy",        "<Control>c" },
    { "win.cut",         "<Control>x" },
    { "win.paste",       "<Control>v" },
    { "win.trash",       "Delete" },
    { "win.new-folder",  "<Control><Shift>n" },
    { "win.refresh",     "<Control>r" },
    { "win.open",        "<Control>o" },
    { "win.new-window",  "<Control>n" },
    { "win.close-window","<Control>w" },
    { "win.go-back",     "<Alt>Left" },
    { "win.go-forward",  "<Alt>Right" },
  };
  gsize i;

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);

  app = gtk_window_get_application (GTK_WINDOW (self));
  if (app)
    {
      for (i = 0; i < G_N_ELEMENTS (accels); i++)
        {
          const char *accel_list[] = { accels[i].accel, NULL };
          gtk_application_set_accels_for_action (app, accels[i].action, accel_list);
        }
    }

  spot_update_action_states (self);
}

static GIcon *
spot_info_get_icon (GFileInfo *info)
{
  GIcon *icon;
  GFileType type;

  if (!info)
    return g_themed_icon_new ("text-x-generic");

  icon = g_file_info_get_icon (info);
  if (icon)
    return g_object_ref (icon);

  icon = g_file_info_get_symbolic_icon (info);
  if (icon)
    return g_object_ref (icon);

  if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
    {
      const char *content_type = g_file_info_get_content_type (info);

      if (content_type)
        return g_content_type_get_icon (content_type);
    }

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    return g_themed_icon_new ("folder");

  return g_themed_icon_new ("text-x-generic");
}

static GtkWidget *
spot_image_new_for_info (GFileInfo *info)
{
  GtkWidget *image;
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkIconTheme *theme;

  image = gtk_image_new ();
  icon = spot_info_get_icon (info);
  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                              icon,
                                              SPOT_LIST_ICON_SIZE,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
  if (!paintable)
    paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                                icon,
                                                SPOT_LIST_ICON_SIZE,
                                                1,
                                                GTK_TEXT_DIR_LTR,
                                                GTK_ICON_LOOKUP_PRELOAD |
                                                GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));
  else
    gtk_image_set_from_gicon (GTK_IMAGE (image), icon);

  gtk_image_set_pixel_size (GTK_IMAGE (image), SPOT_LIST_ICON_SIZE);
  gtk_widget_set_size_request (image, SPOT_LIST_ICON_SIZE, SPOT_LIST_ICON_SIZE);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
  return image;
}

static GtkWidget *
spot_image_new_for_info_size (GFileInfo *info, int size)
{
  GtkWidget *image;
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkIconTheme *theme;

  image = gtk_image_new ();
  icon = spot_info_get_icon (info);
  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                              icon,
                                              size,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
  if (!paintable)
    paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                                icon,
                                                size,
                                                1,
                                                GTK_TEXT_DIR_LTR,
                                                GTK_ICON_LOOKUP_PRELOAD |
                                                GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));
  else
    gtk_image_set_from_gicon (GTK_IMAGE (image), icon);

  gtk_image_set_pixel_size (GTK_IMAGE (image), size);
  gtk_widget_set_size_request (image, size, size);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (image, "ooze-icon");
  return image;
}

/* ── Grid-view helpers ──────────────────────────────────────────────────── */

static GtkWidget *
spot_create_grid_cell (SpotWindow *self, GFileInfo *info, GFile *file)
{
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *label;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_add_css_class (box, "spot-grid-cell");
  gtk_widget_set_size_request (box, SPOT_GRID_CELL_WIDTH, -1);
  gtk_widget_set_valign (box, GTK_ALIGN_START);
  /* Inner breathing room set here, not in CSS */
  gtk_widget_set_margin_top    (box, 3);
  gtk_widget_set_margin_bottom (box, 4);
  gtk_widget_set_margin_start  (box, 3);
  gtk_widget_set_margin_end    (box, 3);

  image = spot_image_new_for_info_size (info, SPOT_GRID_ICON_SIZE);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), image);

  label = gtk_label_new (g_file_info_get_display_name (info));
  gtk_widget_add_css_class (label, "spot-grid-label");
  gtk_label_set_xalign (GTK_LABEL (label), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 12);
  gtk_label_set_lines (GTK_LABEL (label), 2);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_widget_set_margin_top (label, 1);
  gtk_box_append (GTK_BOX (box), label);

  g_object_set_data_full (G_OBJECT (box), "spot-file",
                          g_object_ref (file), g_object_unref);
  spot_attach_file_drag (box, self, file);
  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_attach_folder_drop (box, self, file);
  return box;
}

static void
spot_clear_grid (SpotWindow *self)
{
  GtkWidget *child;

  /* Only remove real FlowBox children — ignore any stray non-item widgets. */
  child = gtk_widget_get_first_child (self->grid_flow);
  while (child != NULL)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);

      if (GTK_IS_FLOW_BOX_CHILD (child))
        gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), child);

      child = next;
    }
}

static void
spot_grid_enumeration_free (SpotGridEnumeration *enumeration)
{
  if (!enumeration)
    return;

  if (enumeration->window->grid_enumeration == enumeration)
    {
      enumeration->window->grid_enumeration = NULL;
      g_clear_object (&enumeration->window->reveal_target);
    }
  g_clear_object (&enumeration->enumerator);
  g_clear_object (&enumeration->directory);
  g_clear_object (&enumeration->cancellable);
  g_object_unref (enumeration->window);
  g_free (enumeration);
}

static void
spot_grid_enumeration_cancel (SpotWindow *self)
{
  SpotGridEnumeration *enumeration = self->grid_enumeration;

  if (!enumeration)
    return;

  g_cancellable_cancel (enumeration->cancellable);
  self->grid_enumeration = NULL;
}

static void
spot_grid_append_info (SpotGridEnumeration *enumeration,
                       GFileInfo           *info)
{
  SpotWindow *self = enumeration->window;
  GtkWidget *loading;
  g_autoptr (GFile) child = NULL;
  GtkWidget *cell;
  GtkWidget *fbc;

  if (g_file_info_get_is_hidden (info))
    return;

  loading = gtk_widget_get_first_child (self->grid_flow);
  if (loading && g_object_get_data (G_OBJECT (loading), "spot-loading"))
    gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), loading);

  child = g_file_get_child (enumeration->directory,
                            g_file_info_get_name (info));
  cell = spot_create_grid_cell (self, info, child);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), cell);

  fbc = gtk_widget_get_parent (cell);
  if (fbc)
    {
      gtk_widget_set_margin_top (fbc, 0);
      gtk_widget_set_margin_bottom (fbc, 0);
      gtk_widget_set_margin_start (fbc, 0);
      gtk_widget_set_margin_end (fbc, 0);
      gtk_widget_set_valign (fbc, GTK_ALIGN_START);
    }

  if (self->reveal_target)
    spot_select_file_widget (self, self->grid_flow, self->reveal_target);
}

static void
spot_grid_enumerate_next_cb (GObject      *source,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  SpotGridEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;
  GList *infos;
  GList *link;

  infos = g_file_enumerator_next_files_finish (
    G_FILE_ENUMERATOR (source), result, &error);
  if (error || !infos)
    {
      spot_grid_enumeration_free (enumeration);
      return;
    }

  for (link = infos; link; link = link->next)
    {
      spot_grid_append_info (enumeration, link->data);
      g_object_unref (link->data);
    }
  g_list_free (infos);

  g_file_enumerator_next_files_async (
    enumeration->enumerator, 32, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_next_cb, enumeration);
}

static void
spot_grid_enumerate_children_cb (GObject      *source,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  SpotGridEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;

  enumeration->enumerator =
    g_file_enumerate_children_finish (G_FILE (source), result, &error);
  if (error || !enumeration->enumerator)
    {
      spot_grid_enumeration_free (enumeration);
      return;
    }

  g_file_enumerator_next_files_async (
    enumeration->enumerator, 32, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_next_cb, enumeration);
}

static void
spot_populate_grid (SpotWindow *self)
{
  SpotGridEnumeration *enumeration;

  spot_grid_enumeration_cancel (self);
  spot_clear_grid (self);

  if (!self->current_dir)
    return;

  {
    GtkWidget *loading = gtk_label_new ("Loading…");

    gtk_widget_set_margin_top (loading, 24);
    gtk_widget_set_margin_start (loading, 24);
    g_object_set_data (G_OBJECT (loading), "spot-loading",
                       GINT_TO_POINTER (1));
    gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), loading);
  }

  enumeration = g_new0 (SpotGridEnumeration, 1);
  enumeration->window = g_object_ref (self);
  enumeration->directory = g_object_ref (self->current_dir);
  enumeration->cancellable = g_cancellable_new ();
  self->grid_enumeration = enumeration;

  g_file_enumerate_children_async (
    enumeration->directory,
    G_FILE_ATTRIBUTE_STANDARD_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_ICON ","
    G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
    G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_children_cb, enumeration);
}

static void
on_grid_child_activated (GtkFlowBox      *box G_GNUC_UNUSED,
                         GtkFlowBoxChild *child,
                         SpotWindow      *self)
{
  GtkWidget *cell = gtk_flow_box_child_get_child (child);
  GFile *file = g_object_get_data (G_OBJECT (cell), "spot-file");

  if (!file)
    return;

  g_autoptr (GFileInfo) info = g_file_query_info (
      file,
      G_FILE_ATTRIBUTE_STANDARD_TYPE,
      G_FILE_QUERY_INFO_NONE,
      NULL, NULL);

  if (info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}

/* ── View-mode switching ────────────────────────────────────────────────── */

static void
spot_set_view_mode (SpotWindow *self, SpotViewMode mode)
{
  GtkWidget *peers[2];

  self->view_mode = mode;

  peers[0] = self->grid_view_button;
  peers[1] = self->column_view_button;
  ooze_button_set_exclusive (peers, 2,
                             mode == SPOT_VIEW_GRID ? 0 : 1);

  gtk_stack_set_visible_child_name (
      GTK_STACK (self->content_stack),
      mode == SPOT_VIEW_GRID ? "grid" : "columns");

  if (mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else
    spot_rebuild_columns (self);
}

static void
on_grid_view_clicked (GtkButton *btn G_GNUC_UNUSED, SpotWindow *self)
{
  spot_set_view_mode (self, SPOT_VIEW_GRID);
}

static void
on_column_view_clicked (GtkButton *btn G_GNUC_UNUSED, SpotWindow *self)
{
  spot_set_view_mode (self, SPOT_VIEW_COLUMNS);
}

static GtkWidget *
spot_image_new_from_icon_list (const char * const *icon_names,
                               int                   size,
                               gboolean              prefer_color G_GNUC_UNUSED)
{
  return ooze_icon_image_new (icon_names, size);
}

static const char * const spot_icon_back[] = {
  "go-previous", "go-previous-symbolic", NULL
};
static const char * const spot_icon_forward[] = {
  "go-next", "go-next-symbolic", NULL
};
static const char * const spot_icon_view_grid[] = {
  "view-grid", "view-app-grid", "view-grid-symbolic", NULL
};
static const char * const spot_icon_view_column[] = {
  "view-column", "view-dual", "view-column-symbolic", "view-dual-symbolic", NULL
};
static const char * const spot_icon_computer[] = {
  "computer", "drive-harddisk", NULL
};
static const char * const spot_icon_home[] = {
  "user-home", "go-home", NULL
};
static const char * const spot_icon_favorites[] = {
  "folder-documents", "folder", NULL
};
static const char * const spot_icon_applications[] = {
  "application-default-icon", "application-x-executable", "system-run", NULL
};
static const char * const spot_icon_drive[] = {
  "drive-harddisk", "drive-harddisk-symbolic", NULL
};

static const char * const *
spot_sidebar_icon_names (const SpotPlace *place)
{
  if (place->use_home)
    return spot_icon_home;

  switch (place->dir)
    {
    case G_USER_DIRECTORY_DESKTOP:
      {
        static const char * const icons[] = { "user-desktop", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_DOCUMENTS:
      {
        static const char * const icons[] = { "folder-documents", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_DOWNLOAD:
      {
        static const char * const icons[] = { "folder-download", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_PICTURES:
      {
        static const char * const icons[] = { "folder-pictures", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_MUSIC:
      {
        static const char * const icons[] = { "folder-music", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_VIDEOS:
      {
        static const char * const icons[] = { "folder-videos", "folder", NULL };
        return icons;
      }
    default:
      {
        static const char * const icons[] = { "folder", NULL };
        return icons;
      }
    }
}

static GtkWidget *
spot_create_toolbar_button (const char * const *icon_names,
                            const char         *label,
                            const char         *tooltip,
                            gboolean            toggle,
                            gboolean            active)
{
  GtkWidget *button;
  GtkWidget *child;

  button = ooze_button_new_toolbar (icon_names, label, tooltip);
  if (toggle && active)
    ooze_button_set_toggled (button, TRUE);

  /* Cap label width so the toolbar cannot push window min past half-screen. */
  child = gtk_button_get_child (GTK_BUTTON (button));
  if (GTK_IS_BOX (child))
    {
      GtkWidget *w = gtk_widget_get_first_child (child);
      while (w)
        {
          if (GTK_IS_LABEL (w))
            {
              gtk_label_set_ellipsize (GTK_LABEL (w), PANGO_ELLIPSIZE_END);
              gtk_label_set_max_width_chars (GTK_LABEL (w), 11);
            }
          w = gtk_widget_get_next_sibling (w);
        }
    }

  return button;
}

static GtkWidget *
spot_create_sidebar_row (const char          *label,
                         const char          *path,
                         const char * const *icon_names)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *lbl;

  row = gtk_list_box_row_new ();
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  image = spot_image_new_from_icon_list (icon_names, SPOT_SIDEBAR_ICON_SIZE, TRUE);
  lbl = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.5);
  gtk_widget_add_css_class (lbl, "spot-sidebar-label");
  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), lbl);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
  g_object_set_data_full (G_OBJECT (row),
                          "spot-path",
                          g_strdup (path),
                          g_free);
  return row;
}

static void
on_nav_computer_clicked (GtkButton *button G_GNUC_UNUSED,
                         gpointer   user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), "/", TRUE);
}

static void
on_nav_home_clicked (GtkButton *button G_GNUC_UNUSED,
                       gpointer   user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), g_get_home_dir (), TRUE);
}

static void
on_nav_favorites_clicked (GtkButton *button G_GNUC_UNUSED,
                            gpointer   user_data)
{
  g_autofree char *path = spot_special_dir_path_dup (G_USER_DIRECTORY_DOCUMENTS,
                                                     "Documents");

  if (!path)
    path = g_strdup (g_get_home_dir ());
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), path, TRUE);
}

static void
on_nav_applications_clicked (GtkButton *button G_GNUC_UNUSED,
                             gpointer   user_data G_GNUC_UNUSED)
{
  spot_launch_pak ();
}

static void
on_new_folder_shortcut (GtkEventControllerKey *controller G_GNUC_UNUSED,
                        guint                    keyval,
                        guint                    keycode G_GNUC_UNUSED,
                        GdkModifierType          state,
                        SpotWindow              *self)
{
  if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
      (GDK_CONTROL_MASK | GDK_SHIFT_MASK) &&
      (keyval == GDK_KEY_n || keyval == GDK_KEY_N))
    spot_show_new_folder_popover (self);
}

static guint
spot_max_columns_for_width (int width)
{
  guint by_space;

  if (width < SPOT_COLUMN_WIDTH)
    return SPOT_MIN_COLUMNS;

  by_space = (guint) (width / SPOT_COLUMN_WIDTH);
  if (by_space <= SPOT_MIN_COLUMNS)
    return SPOT_MIN_COLUMNS;

  return by_space;
}

static void
spot_scroll_columns_to_end (SpotWindow *self)
{
  GtkAdjustment *adj;

  if (!self->columns_scrolled)
    return;

  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->columns_scrolled));
  if (!adj)
    return;

  gtk_adjustment_set_value (adj,
                            gtk_adjustment_get_upper (adj) -
                            gtk_adjustment_get_page_size (adj));
}

static char *
spot_unique_folder_name (GFile *parent)
{
  int suffix = 0;

  if (!parent)
    return g_strdup ("untitled folder");

  while (TRUE)
    {
      g_autofree char *name = NULL;
      g_autoptr (GFile) child = NULL;

      if (suffix == 0)
        name = g_strdup ("untitled folder");
      else
        name = g_strdup_printf ("untitled folder %d", suffix);

      child = g_file_get_child (parent, name);
      if (!g_file_query_exists (child, NULL))
        return g_steal_pointer (&name);

      suffix++;
    }
}

static void
spot_show_error (SpotWindow *self,
                 const char *heading,
                 const char *body)
{
  AdwDialog *dialog;

  dialog = ADW_DIALOG (adw_alert_dialog_new (heading, body));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "ok", "OK");
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "ok",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "ok");
  g_signal_connect (dialog, "response", G_CALLBACK (adw_dialog_close), NULL);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static gboolean
spot_create_folder_named (SpotWindow *self,
                          const char *name)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) new_dir = NULL;

  if (!self->current_dir || !name || name[0] == '\0')
    return FALSE;

  if (strchr (name, '/') || strchr (name, G_DIR_SEPARATOR))
    {
      spot_show_error (self,
                       "Invalid Folder Name",
                       "Folder names cannot contain “/”.");
      return FALSE;
    }

  new_dir = g_file_get_child (self->current_dir, name);
  if (!g_file_make_directory (new_dir, NULL, &error))
    {
      spot_show_error (self, "Could Not Create Folder", error->message);
      return FALSE;
    }

  spot_refresh (self);
  return TRUE;
}

static void
spot_on_new_folder_create (GtkButton  *button G_GNUC_UNUSED,
                           SpotWindow *self)
{
  const char *name;

  if (!self->new_folder_entry)
    return;

  name = gtk_editable_get_text (GTK_EDITABLE (self->new_folder_entry));
  if (spot_create_folder_named (self, name) && self->new_folder_popover)
    gtk_popover_popdown (GTK_POPOVER (self->new_folder_popover));
}

static void
spot_on_new_folder_entry_activate (GtkEntry   *entry G_GNUC_UNUSED,
                                   SpotWindow *self)
{
  spot_on_new_folder_create (NULL, self);
}

static void
spot_show_new_folder_popover (SpotWindow *self)
{
  g_autofree char *default_name = NULL;

  if (!self->current_dir || !self->toolbar)
    return;

  if (!self->new_folder_popover)
    {
      GtkWidget *box;
      GtkWidget *label;
      GtkWidget *create_button;

      self->new_folder_popover = gtk_popover_new ();
      gtk_widget_set_parent (self->new_folder_popover, self->toolbar);

      box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
      gtk_widget_set_margin_top (box, 8);
      gtk_widget_set_margin_bottom (box, 8);
      gtk_widget_set_margin_start (box, 12);
      gtk_widget_set_margin_end (box, 12);

      label = gtk_label_new ("Folder name:");
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_box_append (GTK_BOX (box), label);

      self->new_folder_entry = gtk_entry_new ();
      gtk_widget_set_size_request (self->new_folder_entry, 220, -1);
      gtk_box_append (GTK_BOX (box), self->new_folder_entry);
      g_signal_connect (self->new_folder_entry,
                        "activate",
                        G_CALLBACK (spot_on_new_folder_entry_activate),
                        self);

      create_button = gtk_button_new_with_label ("Create");
      gtk_widget_set_halign (create_button, GTK_ALIGN_END);
      g_signal_connect (create_button, "clicked", G_CALLBACK (spot_on_new_folder_create), self);
      gtk_box_append (GTK_BOX (box), create_button);

      gtk_popover_set_child (GTK_POPOVER (self->new_folder_popover), box);
    }

  default_name = spot_unique_folder_name (self->current_dir);
  gtk_editable_set_text (GTK_EDITABLE (self->new_folder_entry), default_name);
  gtk_popover_popup (GTK_POPOVER (self->new_folder_popover));
  gtk_entry_grab_focus_without_selecting (GTK_ENTRY (self->new_folder_entry));
  gtk_editable_select_region (GTK_EDITABLE (self->new_folder_entry), 0, -1);
}

static int
spot_compare_file_info (gconstpointer a,
                        gconstpointer b)
{
  GFileInfo *ia = (GFileInfo *) a;
  GFileInfo *ib = (GFileInfo *) b;
  gboolean dir_a;
  gboolean dir_b;
  const char *name_a;
  const char *name_b;

  dir_a = g_file_info_get_file_type (ia) == G_FILE_TYPE_DIRECTORY;
  dir_b = g_file_info_get_file_type (ib) == G_FILE_TYPE_DIRECTORY;
  if (dir_a != dir_b)
    return dir_b - dir_a;

  name_a = g_file_info_get_display_name (ia);
  name_b = g_file_info_get_display_name (ib);
  return g_ascii_strcasecmp (name_a, name_b);
}

static GFile *
spot_column_browser_root (GFile *dir)
{
  g_autoptr (GFile) best = NULL;
  gsize i;

  if (!dir)
    return NULL;

  /* Home and special dirs first — longest / deepest match wins. */
  for (i = 0; i < G_N_ELEMENTS (sidebar_places); i++)
    {
      g_autofree char *path = NULL;
      g_autoptr (GFile) place = NULL;

      path = spot_place_path_dup (&sidebar_places[i]);
      if (!path)
        continue;

      place = g_file_new_for_path (path);
      if (!g_file_equal (dir, place) && !g_file_has_prefix (dir, place))
        continue;

      if (!best || g_file_has_prefix (place, best) || g_file_equal (place, best))
        g_set_object (&best, place);
    }

  /* Fallback: filesystem root ("Linux HD") */
  if (!best)
    best = g_file_new_for_path ("/");

  return g_steal_pointer (&best);
}

/* Path chain from browser_root down to dir (inclusive). */
static GList *
spot_path_chain_from_root (GFile *dir,
                           GFile *root)
{
  GList *chain = NULL;
  GFile *current;

  if (!dir || !root)
    return NULL;

  current = g_object_ref (dir);
  while (current)
    {
      gboolean at_root = g_file_equal (current, root);

      chain = g_list_prepend (chain, current);
      if (at_root)
        break;

      {
        g_autoptr (GFile) parent = g_file_get_parent (current);

        current = parent ? g_object_ref (parent) : NULL;
      }
    }

  return chain;
}

static void
spot_update_nav_buttons (SpotWindow *self)
{
  gtk_widget_set_sensitive (self->back_button, self->back_stack != NULL);
  gtk_widget_set_sensitive (self->forward_button, self->forward_stack != NULL);
}

static void
spot_update_title (SpotWindow *self)
{
  if (!self->current_dir)
    {
      ooze_application_window_set_title (
        OOZE_APPLICATION_WINDOW (self), "Spot");
      return;
    }

  {
    g_autofree char *basename = g_file_get_basename (self->current_dir);
    const char *title = basename;

    if (!title || title[0] == '\0')
      title = "/";

    ooze_application_window_set_title (
      OOZE_APPLICATION_WINDOW (self), title);
  }
}

static void
spot_update_status (SpotWindow *self)
{
  int count = 0;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  if (!self->current_dir)
    {
      gtk_label_set_text (GTK_LABEL (self->status_label), "");
      return;
    }

  enumerator = g_file_enumerate_children (self->current_dir,
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);
  if (enumerator)
    {
      GFileInfo *info;

      while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
        {
          if (!g_file_info_get_is_hidden (info))
            count++;
          g_object_unref (info);
        }
    }

  {
    g_autofree char *text = g_strdup_printf ("%d item%s",
                                             count,
                                             count == 1 ? "" : "s");
    gtk_label_set_text (GTK_LABEL (self->status_label), text);
  }
}

static void
spot_set_current_dir (SpotWindow *self,
                      GFile      *dir)
{
  g_set_object (&self->current_dir, dir);
  spot_update_nav_buttons (self);
  spot_update_title (self);
}

static void
spot_push_history (SpotWindow *self,
                   GFile      *dir)
{
  if (!self->current_dir)
    return;

  self->back_stack = g_list_prepend (self->back_stack,
                                     g_object_ref (self->current_dir));
  g_list_free_full (self->forward_stack, g_object_unref);
  self->forward_stack = NULL;
  spot_set_current_dir (self, dir);
}

static void
spot_pop_history (SpotWindow *self,
                  GList     **stack,
                  GList     **other_stack)
{
  GList *link;
  GFile *dir;

  if (!*stack)
    return;

  link = *stack;
  dir = link->data;
  *stack = g_list_remove_link (*stack, link);
  g_list_free_1 (link);

  if (self->current_dir)
    *other_stack = g_list_prepend (*other_stack,
                                   g_object_ref (self->current_dir));

  spot_set_current_dir (self, g_object_ref (dir));
  g_object_unref (dir);
}

static void
spot_launch_uri (const char *uri)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GAppLaunchContext) ctx = g_app_launch_context_new ();
  g_autoptr (GFile) file = NULL;
  g_autoptr (GAppInfo) info = NULL;

  file = g_file_new_for_uri (uri);
  info = g_file_query_default_handler (file, NULL, NULL);
  ooze_appmenu_prepare_launch_context_for_info (ctx, info);

  if (info)
    {
      GList uris = { .data = (gpointer) uri, .next = NULL, .prev = NULL };

      if (!g_app_info_launch_uris (info, &uris, ctx, &error))
        g_warning ("Spot: failed to open %s: %s", uri, error->message);
      return;
    }

  if (!g_app_info_launch_default_for_uri (uri, ctx, &error))
    g_warning ("Spot: failed to open %s: %s", uri, error->message);
}

static void
spot_open_file (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info)
    return;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    return;

  spot_launch_uri (g_file_get_uri (file));
}

static GtkWidget *
spot_create_column_list (SpotWindow *self,
                         GFile      *directory,
                         GFile      *select_child)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GtkWidget *scrolled;
  GtkWidget *list;
  GtkWidget *selected_row = NULL;

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled, SPOT_COLUMN_WIDTH, -1);
  gtk_widget_set_hexpand (scrolled, FALSE);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_widget_add_css_class (scrolled, "spot-column");

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  /* Miller columns: single-click drills into the next column. */
  gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (list), TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);

  enumerator = g_file_enumerate_children (directory,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);
  if (!enumerator)
    {
      GtkWidget *row = gtk_list_box_row_new ();
      GtkWidget *label = gtk_label_new (error ? error->message : "Unable to read folder");
      gtk_widget_set_margin_start (label, 6);
      gtk_widget_set_margin_end (label, 6);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
      return scrolled;
    }

  GList *entries = NULL;
  GList *l;
  GFileInfo *info;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      if (g_file_info_get_is_hidden (info))
        {
          g_object_unref (info);
          continue;
        }

      entries = g_list_prepend (entries, info);
    }

  entries = g_list_sort (entries, spot_compare_file_info);

  for (l = entries; l != NULL; l = l->next)
    {
      GFileInfo *entry = l->data;
      GtkWidget *row;
      GtkWidget *box;
      GtkWidget *image;
      GtkWidget *label;
      g_autoptr (GFile) child = NULL;
      const char *display_name;

      child = g_file_get_child (directory, g_file_info_get_name (entry));
      row = gtk_list_box_row_new ();
      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_widget_set_margin_top (box, 2);
      gtk_widget_set_margin_bottom (box, 2);

      image = spot_image_new_for_info (entry);

      display_name = g_file_info_get_display_name (entry);
      label = gtk_label_new (display_name);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

      gtk_box_append (GTK_BOX (box), image);
      gtk_box_append (GTK_BOX (box), label);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

      g_object_set_data_full (G_OBJECT (row), "spot-file", g_object_ref (child), g_object_unref);
      spot_attach_file_drag (row, self, child);
      if (g_file_info_get_file_type (entry) == G_FILE_TYPE_DIRECTORY)
        spot_attach_folder_drop (row, self, child);
      gtk_list_box_append (GTK_LIST_BOX (list), row);

      if (select_child && g_file_equal (child, select_child))
        selected_row = row;
    }

  g_list_free_full (entries, g_object_unref);

  if (selected_row)
    gtk_list_box_select_row (GTK_LIST_BOX (list), GTK_LIST_BOX_ROW (selected_row));

  g_signal_connect (list, "row-activated", G_CALLBACK (spot_on_column_row_activated), self);
  spot_attach_context_menu (self, list);

  return scrolled;
}

static void
spot_clear_columns (SpotWindow *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->columns_box)) != NULL)
    gtk_box_remove (GTK_BOX (self->columns_box), child);
}

static void
spot_rebuild_columns (SpotWindow *self)
{
  g_autoptr (GFile) root = NULL;
  g_autolist (GFile) chain = NULL;
  guint len;
  guint i;
  guint max_columns;
  guint start;
  int width;

  spot_clear_columns (self);

  if (!self->current_dir)
    return;

  root = spot_column_browser_root (self->current_dir);
  chain = spot_path_chain_from_root (self->current_dir, root);
  len = g_list_length (chain);
  if (len == 0)
    return;

  width = gtk_widget_get_width (self->columns_scrolled);
  if (width <= 0)
    width = SPOT_COLUMN_WIDTH * SPOT_MIN_COLUMNS;

  /* Track the width-based capacity (not path length) so notify::width
   * does not rebuild in a loop when the path is shorter than the pane. */
  max_columns = spot_max_columns_for_width (width);
  self->last_column_count = max_columns;
  if (max_columns > len)
    max_columns = len;

  start = len - max_columns;

  for (i = start; i < len; i++)
    {
      GFile *dir = g_list_nth_data (chain, i);
      GFile *select = (i + 1 < len) ? g_list_nth_data (chain, i + 1) : NULL;
      GtkWidget *column;

      if (i > start)
        gtk_box_append (GTK_BOX (self->columns_box),
                        ooze_pinline_new (OOZE_SIDE_RIGHT));

      column = spot_create_column_list (self, dir, select);
      gtk_box_append (GTK_BOX (self->columns_box), column);
    }

  spot_scroll_columns_to_end (self);
}

static void
spot_on_columns_width_changed (GObject    *object G_GNUC_UNUSED,
                               GParamSpec *pspec G_GNUC_UNUSED,
                               SpotWindow *self)
{
  int width;
  guint max_columns;

  if (!self->current_dir)
    return;

  width = gtk_widget_get_width (self->columns_scrolled);
  max_columns = spot_max_columns_for_width (width);

  if (max_columns == self->last_column_count)
    return;

  spot_rebuild_columns (self);
}

static void
spot_refresh (SpotWindow *self)
{
  if (self->view_mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else
    spot_rebuild_columns (self);

  spot_update_nav_buttons (self);
  spot_update_title (self);
  spot_update_status (self);
  spot_update_action_states (self);
}

static void
spot_navigate_to (SpotWindow *self,
                  GFile      *dir,
                  gboolean    push_history)
{
  g_autoptr (GFileInfo) info = NULL;

  if (!dir)
    return;

  info = g_file_query_info (dir,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info || g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
    return;

  if (push_history && self->current_dir &&
      !g_file_equal (self->current_dir, dir))
    spot_push_history (self, dir);
  else
    spot_set_current_dir (self, g_object_ref (dir));

  spot_refresh (self);
}

void
spot_window_open_path (SpotWindow *self,
                       const char *path)
{
  g_autoptr (GFile) dir = NULL;

  if (!path || path[0] == '\0')
    return;

  if (g_str_has_prefix (path, "file://") ||
      g_str_has_prefix (path, "trash://"))
    dir = g_file_new_for_uri (path);
  else
    dir = g_file_new_for_path (path);

  spot_navigate_to (self, dir, FALSE);
}

void
spot_window_reveal_uri (SpotWindow   *self,
                        const char   *uri)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GFile) parent = NULL;

  if (!self || !uri || !*uri)
    return;

  file = g_file_new_for_uri (uri);
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL, NULL);
  if (!info)
    return;

  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
      g_autofree char *path = g_file_get_path (file);

      if (path)
        {
          spot_set_view_mode (self, SPOT_VIEW_GRID);
          spot_window_open_path (self, path);
        }
      return;
    }

  parent = g_file_get_parent (file);
  if (!parent)
    return;

  spot_set_view_mode (self, SPOT_VIEW_GRID);
  g_set_object (&self->reveal_target, file);
  spot_navigate_to (self, parent, FALSE);
}

static void
spot_navigate_to_path_string (SpotWindow *self,
                              const char *path,
                              gboolean    push_history)
{
  g_autoptr (GFile) dir = g_file_new_for_path (path);

  spot_navigate_to (self, dir, push_history);
}

static void
on_back_clicked (GtkButton *button G_GNUC_UNUSED,
                 gpointer   user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  spot_pop_history (self, &self->back_stack, &self->forward_stack);
  spot_refresh (self);
}

static void
on_forward_clicked (GtkButton *button G_GNUC_UNUSED,
                    gpointer   user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  spot_pop_history (self, &self->forward_stack, &self->back_stack);
  spot_refresh (self);
}

static void
spot_on_column_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                              GtkListBoxRow *row,
                              gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = g_object_get_data (G_OBJECT (row), "spot-file");
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  if (!file)
    return;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info)
    return;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}

static void
on_sidebar_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                          GtkListBoxRow *row,
                          gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  const char *path = g_object_get_data (G_OBJECT (row), "spot-path");

  if (path)
    spot_navigate_to_path_string (self, path, TRUE);
}

static GtkWidget *
spot_create_sidebar (SpotWindow *self)
{
  GtkWidget *bin;
  GtkWidget *scrolled;
  GtkWidget *list;
  gsize i;

  /* OozeSurface draws the sidebar chrome; the scrolled window inside is
   * transparent so the surface colour shows through. */
  bin = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (bin, 88, -1);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  /* CSS class on the list so row selection/hover selectors still work */
  gtk_widget_add_css_class (list, "spot-sidebar-list");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);
  gtk_box_append (GTK_BOX (bin), scrolled);

  gtk_list_box_append (GTK_LIST_BOX (list),
                       spot_create_sidebar_row ("Linux HD",
                                                "/",
                                                spot_icon_drive));

  for (i = 0; i < G_N_ELEMENTS (sidebar_places); i++)
    {
      g_autofree char *path = spot_place_path_dup (&sidebar_places[i]);

      if (!path)
        continue;

      gtk_list_box_append (GTK_LIST_BOX (list),
                           spot_create_sidebar_row (sidebar_places[i].label,
                                                    path,
                                                    spot_sidebar_icon_names (&sidebar_places[i])));
    }

  g_signal_connect (list, "row-activated", G_CALLBACK (on_sidebar_row_activated), self);
  self->sidebar = list;

  return bin;
}

static GtkWidget *
spot_create_toolbar (SpotWindow *self)
{
  GtkWidget *toolbar;
  GtkWidget *nav;
  GtkWidget *view;
  GtkWidget *places;
  GtkWidget *computer_btn;
  GtkWidget *home_btn;
  GtkWidget *favorites_btn;
  GtkWidget *applications_btn;

  toolbar = ooze_toolbar_new ();

  nav = ooze_toolbar_add_group (toolbar);
  self->back_button = spot_create_toolbar_button (spot_icon_back, "Back", "Back",
                                                   FALSE, FALSE);
  gtk_widget_add_css_class (self->back_button, "ooze-nav-btn");
  self->forward_button = spot_create_toolbar_button (spot_icon_forward, "Forward", "Forward",
                                                     FALSE, FALSE);
  gtk_widget_add_css_class (self->forward_button, "ooze-nav-btn");
  gtk_box_append (GTK_BOX (nav), self->back_button);
  gtk_box_append (GTK_BOX (nav), self->forward_button);

  ooze_toolbar_add_separator (toolbar);

  view = ooze_toolbar_add_group (toolbar);
  self->grid_view_button =
    spot_create_toolbar_button (spot_icon_view_grid, "Grid", "Grid view", TRUE, TRUE);
  self->column_view_button =
    spot_create_toolbar_button (spot_icon_view_column, "Columns", "Columns view", TRUE, FALSE);
  gtk_box_append (GTK_BOX (view), self->grid_view_button);
  gtk_box_append (GTK_BOX (view), self->column_view_button);

  ooze_toolbar_add_separator (toolbar);

  places = ooze_toolbar_add_group (toolbar);
  computer_btn = spot_create_toolbar_button (spot_icon_computer, "Computer", "Computer",
                                             FALSE, FALSE);
  home_btn = spot_create_toolbar_button (spot_icon_home, "Home", "Home", FALSE, FALSE);
  favorites_btn = spot_create_toolbar_button (spot_icon_favorites, "Favorites", "Favorites",
                                              FALSE, FALSE);
  applications_btn = spot_create_toolbar_button (spot_icon_applications, "Applications",
                                                 "Applications", FALSE, FALSE);
  gtk_box_append (GTK_BOX (places), computer_btn);
  gtk_box_append (GTK_BOX (places), home_btn);
  gtk_box_append (GTK_BOX (places), favorites_btn);
  gtk_box_append (GTK_BOX (places), applications_btn);

  ooze_toolbar_add_spacer (toolbar);

  self->search_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->search_entry), "Search");
  gtk_widget_add_css_class (self->search_entry, "ooze-toolbar-search");
  gtk_widget_add_css_class (self->search_entry, "spot-search");
  gtk_widget_set_valign (self->search_entry, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (self->search_entry, GTK_ALIGN_END);
  gtk_widget_set_hexpand (self->search_entry, FALSE);
  gtk_box_append (GTK_BOX (toolbar), self->search_entry);

  g_signal_connect (self->back_button, "clicked", G_CALLBACK (on_back_clicked), self);
  g_signal_connect (self->forward_button, "clicked", G_CALLBACK (on_forward_clicked), self);
  g_signal_connect (self->grid_view_button, "clicked", G_CALLBACK (on_grid_view_clicked), self);
  g_signal_connect (self->column_view_button, "clicked", G_CALLBACK (on_column_view_clicked), self);
  g_signal_connect (computer_btn, "clicked", G_CALLBACK (on_nav_computer_clicked), self);
  g_signal_connect (home_btn, "clicked", G_CALLBACK (on_nav_home_clicked), self);
  g_signal_connect (favorites_btn, "clicked", G_CALLBACK (on_nav_favorites_clicked), self);
  g_signal_connect (applications_btn, "clicked", G_CALLBACK (on_nav_applications_clicked), self);

  return toolbar;
}

static void
spot_window_constructed (GObject *object)
{
  SpotWindow *self = SPOT_WINDOW (object);
  GtkWidget *shell;
  GtkWidget *content_paned;
  GtkWidget *statusbar;

  G_OBJECT_CLASS (spot_window_parent_class)->constructed (object);

  spot_ensure_css ();

  gtk_window_set_icon_name (GTK_WINDOW (self), "system-file-manager");
  gtk_window_set_default_size (GTK_WINDOW (self), 960, 640);
  /*
   * Do NOT call gtk_window_set_decorated(FALSE).
   * On Wayland, GTK4 uses CSD by default: the Wayland compositor gives
   * us a transparent surface and GTK draws the shadow + rounded frame
   * via the CSS `decoration` node.  We set our OozeHeaderBar as the
   * native titlebar so GTK4 handles drag-to-move and edge-resize
   * automatically – no custom OozeShadowBin machinery needed.
   */
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Spot");

  spot_install_actions (self);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->toolbar = spot_create_toolbar (self);
  /* ooze_gel_install_drag not needed for the toolbar: GTK4 CSD
   * handles window-move via the titlebar widget above. */

  content_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (content_paned, TRUE);
  gtk_widget_set_vexpand (content_paned, TRUE);

  gtk_paned_set_start_child (GTK_PANED (content_paned), spot_create_sidebar (self));
  gtk_paned_set_resize_start_child (GTK_PANED (content_paned), FALSE);
  gtk_paned_set_shrink_start_child (GTK_PANED (content_paned), FALSE);

  /* ── Columns view ───────────────────────────────────────────────── */
  self->columns_scrolled = ooze_scrolled_window_new ();
  /* Horizontal pan only — each column owns its own vertical map. */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_NEVER);
  gtk_scrolled_window_set_propagate_natural_width (
      GTK_SCROLLED_WINDOW (self->columns_scrolled), FALSE);
  gtk_widget_set_hexpand (self->columns_scrolled, TRUE);
  gtk_widget_set_vexpand (self->columns_scrolled, TRUE);

  self->columns_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                 self->columns_box);
  g_signal_connect (self->columns_scrolled,
                    "notify::width",
                    G_CALLBACK (spot_on_columns_width_changed),
                    self);

  /* ── Grid / icon view ───────────────────────────────────────────── */
  self->grid_scrolled = ooze_scrolled_window_new ();
  gtk_widget_add_css_class (self->grid_scrolled, "spot-grid-scroll");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->grid_scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->grid_scrolled, TRUE);
  gtk_widget_set_vexpand (self->grid_scrolled, TRUE);

  self->grid_flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid_flow),
                                   GTK_SELECTION_SINGLE);
  gtk_flow_box_set_activate_on_single_click (GTK_FLOW_BOX (self->grid_flow), FALSE);
  /* homogeneous = FALSE: rows are only as tall as their tallest cell */
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid_flow), FALSE);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->grid_flow), 20);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_widget_set_margin_top    (self->grid_flow, 8);
  gtk_widget_set_margin_bottom (self->grid_flow, 8);
  gtk_widget_set_margin_start  (self->grid_flow, 8);
  gtk_widget_set_margin_end    (self->grid_flow, 8);
  /* Don't distribute leftover space — items pack tight from the top-left */
  gtk_widget_set_halign (self->grid_flow, GTK_ALIGN_START);
  gtk_widget_set_valign (self->grid_flow, GTK_ALIGN_START);
  gtk_widget_add_css_class (self->grid_flow, "spot-grid-view");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->grid_scrolled),
                                 self->grid_flow);
  g_signal_connect (self->grid_flow, "child-activated",
                    G_CALLBACK (on_grid_child_activated), self);
  spot_attach_dir_drop (self->grid_flow, self);
  spot_attach_dir_drop (self->grid_scrolled, self);
  spot_attach_context_menu (self, self->grid_flow);
  spot_attach_context_menu (self, self->grid_scrolled);
  spot_attach_context_menu (self, self->columns_scrolled);

  /* ── Stack wraps both views ─────────────────────────────────────── */
  self->content_stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->content_stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration (GTK_STACK (self->content_stack), 100);
  gtk_widget_set_hexpand (self->content_stack, TRUE);
  gtk_widget_set_vexpand (self->content_stack, TRUE);

  gtk_stack_add_named (GTK_STACK (self->content_stack),
                       self->columns_scrolled, "columns");
  gtk_stack_add_named (GTK_STACK (self->content_stack),
                       self->grid_scrolled, "grid");
  gtk_stack_set_visible_child_name (GTK_STACK (self->content_stack), "grid");
  self->view_mode = SPOT_VIEW_GRID;

  gtk_paned_set_end_child (GTK_PANED (content_paned), self->content_stack);

  statusbar = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (statusbar, "spot-statusbar");
  self->status_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
  gtk_box_append (GTK_BOX (statusbar), self->status_label);

  /* Keep the toolbar in a horizontal scroller so its natural width
   * (Back…Applications + Search) cannot raise the window min-size above
   * half the monitor — Mutter refuses side-tile when min_width is too big.
   */
  {
    GtkWidget *toolbar_scroll = gtk_scrolled_window_new ();

    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                                      TRUE);
    gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                                     FALSE);
    gtk_widget_set_hexpand (toolbar_scroll, TRUE);
    /* Glass rims overhang into toolbar padding — don't clip the strip. */
    gtk_widget_set_overflow (toolbar_scroll, GTK_OVERFLOW_VISIBLE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                   self->toolbar);
    gtk_box_append (GTK_BOX (shell), toolbar_scroll);
  }
  gtk_box_append (GTK_BOX (shell), content_paned);
  gtk_box_append (GTK_BOX (shell), statusbar);

  /* Set content directly – no OozeShadowBin grid wrapper needed. */
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  {
    GtkEventController *keys;

    keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_new_folder_shortcut), self);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);
  }

  spot_update_action_states (self);
  spot_append_menus (self);
}

static void
spot_window_dispose (GObject *object)
{
  SpotWindow *self = SPOT_WINDOW (object);

  spot_spring_cancel (self);
  spot_grid_enumeration_cancel (self);
  spot_window_shell_drag_leave (self);
  g_clear_object (&self->current_dir);
  g_clear_object (&self->context_target);
  g_clear_object (&self->reveal_target);
  if (self->context_menu)
    {
      gtk_widget_unparent (self->context_menu);
      self->context_menu = NULL;
    }
  g_list_free_full (self->back_stack, g_object_unref);
  g_list_free_full (self->forward_stack, g_object_unref);
  self->back_stack = NULL;
  self->forward_stack = NULL;

  G_OBJECT_CLASS (spot_window_parent_class)->dispose (object);
}

static void
spot_window_class_init (SpotWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = spot_window_constructed;
  object_class->dispose = spot_window_dispose;
}

static void
spot_window_init (SpotWindow *self G_GNUC_UNUSED)
{
  /* OozeSurface and OozeButton connect to notify::dark individually, so no
   * whole-window redraw hook is needed here. */
}

SpotWindow *
spot_window_new_for_path (GtkApplication *app,
                          const char     *path)
{
  SpotWindow *window;
  const char *start_path = path;

  window = g_object_new (SPOT_TYPE_WINDOW,
                         "application", app,
                         "standard-edit-actions", FALSE,
                         "standard-menus", FALSE,
                         NULL);

  if (!start_path)
    start_path = g_object_get_data (G_OBJECT (app), "start-path");
  if (!start_path)
    start_path = g_get_home_dir ();

  spot_window_open_path (window, start_path);

  return window;
}

SpotWindow *
spot_window_new (GtkApplication *app)
{
  return spot_window_new_for_path (app, NULL);
}
