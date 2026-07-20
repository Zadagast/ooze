#pragma once

/* Spot internals shared between the window core and its modules:
 * spot-window.c        — window lifecycle, toolbar, navigation, pathbar
 * spot-sidebar.c       — places sidebar
 * spot-view-grid.c     — icon grid view
 * spot-view-list.c     — list view (GtkColumnView)
 * spot-view-columns.c  — Miller columns view
 * spot-fileops.c       — copy/move/trash/clipboard, drag & drop, new folder
 * spot-actions.c       — GActions, menus, context menu
 */

#include "spot-window.h"

#include "ooze-dnd-bridge.h"
#include "ooze-shared-appmenu.h"

/* OozeKit — shared surface + button widgets */
#include "ooze-surface.h"
#include "ooze-button.h"
#include "ooze-icons.h"
#include "ooze-theme.h"
#include "ooze-toolbar.h"
#include "ooze-about.h"
#include "ooze-pinline.h"
#include "ooze-scroll.h"
#include "ooze-popover.h"
#include "ooze-segment.h"

#include <adwaita.h>
#include <string.h>

G_BEGIN_DECLS

#define SPOT_COLUMN_WIDTH     180
#define SPOT_COLUMN_WIDTH_MIN 120
#define SPOT_COLUMN_WIDTH_MAX 420
#define SPOT_MIN_COLUMNS       3
#define SPOT_LIST_ICON_SIZE    OOZE_ICON_SIZE_LIST
#define SPOT_GRID_ICON_SIZE    OOZE_ICON_SIZE_GRID
#define SPOT_GRID_CELL_WIDTH   84
#define SPOT_TOOLBAR_ICON_SIZE OOZE_ICON_SIZE_TOOLBAR
#define SPOT_SIDEBAR_ICON_SIZE OOZE_ICON_SIZE_SIDEBAR

typedef enum {
  SPOT_VIEW_GRID    = 0,  /* default */
  SPOT_VIEW_COLUMNS = 1,
  SPOT_VIEW_LIST    = 2,
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

  /* list view */
  GtkWidget *list_scrolled;
  GtkWidget *list_view;       /* GtkColumnView */
  GListStore *list_store;     /* of GFileInfo (with "spot-file" data) */

  GtkWidget *sidebar;
  GtkWidget *status_label;
  GtkWidget *pathbar;
  GtkWidget *pathbar_surface;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *view_segment;
  char      *search_text;
  GtkFilter *list_filter;
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
  int column_width;
  guint spring_id;
  SpotViewMode view_mode;
  gboolean clipboard_cut;
  gboolean shell_drag_active;
};

typedef struct
{
  const char *label;
  GUserDirectory dir;
  gboolean use_home;
} SpotPlace;

extern const SpotPlace spot_sidebar_places[];
extern const char * const spot_icon_home[];
extern const char * const spot_icon_applications[];
extern const guint spot_sidebar_places_len;

/* cross-module internals */
void on_grid_child_activated (GtkFlowBox *box G_GNUC_UNUSED, GtkFlowBoxChild *child, SpotWindow *self);
void on_new_folder_shortcut (GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state, SpotWindow *self);
void on_view_segment_changed (GtkWidget *segment G_GNUC_UNUSED, int index, SpotWindow *self);
void spot_append_menus (SpotWindow *self);
void spot_attach_context_menu (SpotWindow *self, GtkWidget *widget);
void spot_attach_dir_drop (GtkWidget *widget, SpotWindow *self);
void spot_attach_file_drag (GtkWidget *widget, SpotWindow *self, GFile *file G_GNUC_UNUSED);
void spot_attach_folder_drop (GtkWidget *widget, SpotWindow *self, GFile *folder);
void spot_clipboard_set_files (SpotWindow *self, GFile *file, gboolean cut);
gboolean spot_columns_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state, gpointer user_data);
int spot_compare_file_info (gconstpointer a, gconstpointer b);
GtkWidget * spot_create_list_view (SpotWindow *self);
GtkWidget * spot_create_sidebar (SpotWindow *self);
GFile * spot_get_selected_file (SpotWindow *self);
void spot_grid_enumeration_cancel (SpotWindow *self);
gint spot_grid_sort_func (GtkFlowBoxChild *child_a, GtkFlowBoxChild *child_b, gpointer user_data G_GNUC_UNUSED);
GtkWidget * spot_image_new_for_info (GFileInfo *info);
GtkWidget * spot_image_new_from_icon_list (const char * const *icon_names, int size, gboolean prefer_color G_GNUC_UNUSED);
void spot_install_actions (SpotWindow *self);
void spot_launch_pak (void);
gboolean spot_list_filter_func (gpointer item, gpointer user_data);
void spot_navigate_to (SpotWindow *self, GFile *dir, gboolean push_history);
void spot_navigate_to_path_string (SpotWindow *self, const char *path, gboolean push_history);
void spot_on_column_row_activated (GtkListBox *box G_GNUC_UNUSED, GtkListBoxRow *row, gpointer user_data);
void spot_on_new_folder_create (GtkButton *button G_GNUC_UNUSED, SpotWindow *self);
void spot_open_file (GFile *file);
void spot_paste_files_finish (GObject *source, GAsyncResult *result, gpointer user_data);
GFile * spot_pick_file_at (GtkWidget *widget, double x, double y);
char * spot_place_path_dup (const SpotPlace *place);
void spot_pop_history (SpotWindow *self, GList **stack, GList **other_stack);
void spot_populate_grid (SpotWindow *self);
void spot_populate_list (SpotWindow *self);
void spot_rebuild_columns (SpotWindow *self);
void spot_refresh (SpotWindow *self);
void spot_select_file_widget (SpotWindow *self, GtkWidget *host G_GNUC_UNUSED, GFile *file);
void spot_set_context_target (SpotWindow *self, GFile *file);
void spot_set_view_mode (SpotWindow *self, SpotViewMode mode);
void spot_show_error (SpotWindow *self, const char *heading, const char *body);
void spot_show_new_folder_popover (SpotWindow *self);
char * spot_special_dir_path_dup (GUserDirectory dir, const char *fallback_name);
void spot_spring_cancel (SpotWindow *self);
void spot_state_load (SpotWindow *self);
void spot_update_action_states (SpotWindow *self);

G_END_DECLS
