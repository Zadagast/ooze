#include "spot-priv.h"

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
spot_action_view_list (GSimpleAction *action G_GNUC_UNUSED,
                       GVariant      *param G_GNUC_UNUSED,
                       gpointer       user_data)
{
  spot_set_view_mode (SPOT_WINDOW (user_data), SPOT_VIEW_LIST);
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

void
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

void
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
  g_menu_append (view, "as List", "win.view-list");
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

void
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

void
spot_attach_context_menu (SpotWindow *self,
                          GtkWidget  *widget)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect (gesture, "pressed", G_CALLBACK (on_context_pressed), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}

void
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
    { "view-list",       spot_action_view_list,       NULL, NULL, NULL },
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


