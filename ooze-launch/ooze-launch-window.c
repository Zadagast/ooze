#include "ooze-launch-window.h"

#include "ooze-button.h"
#include "ooze-icons.h"
#include "ooze-scroll.h"
#include "ooze-shared-appmenu.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <gio/gdesktopappinfo.h>

#include <string.h>

typedef struct
{
  GAppInfo *info;
  char     *id;
  char     *name;
  char     *search_text;
  GtkWidget *tile;
} LaunchApp;

typedef struct
{
  char      *name;
  gboolean   custom;
  GPtrArray *app_ids;
  GtkWidget *button;
} LaunchGroup;

struct _OozeLaunchWindow
{
  OozeApplicationWindow parent_instance;
  GPtrArray *apps;
  GPtrArray *groups;
  LaunchGroup *active_group;
  GtkWidget *search;
  GtkWidget *grid;
  GtkWidget *groups_bar;
  GtkWidget *context_popover;
  GtkWidget *add_popover;
  GtkWidget *add_entry;
};

G_DEFINE_FINAL_TYPE (OozeLaunchWindow, ooze_launch_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static const char * const launch_generic_icons[] = {
  "application-x-executable",
  "application-x-generic",
  NULL,
};

static void launch_rebuild_groups_bar (OozeLaunchWindow *self);
static void launch_refresh_grid (OozeLaunchWindow *self);

static void
ooze_launch_ensure_css (void)
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
  gtk_css_provider_load_from_string (
    provider,
    ".ooze-launch {"
    "  background: @window_bg_color;"
    "}"
    ".ooze-launch .ooze-surface,"
    ".ooze-launch scrolledwindow,"
    ".ooze-launch scrolledwindow > viewport,"
    ".ooze-launch .ooze-launcher-grid {"
    "  border-radius: 0;"
    "  border: none;"
    "  box-shadow: none;"
    "  margin: 0;"
    "  outline: none;"
    "}"
    ".ooze-launch .ooze-launch-grid-scroll,"
    ".ooze-launch .ooze-launch-grid-scroll > viewport,"
    ".ooze-launch .ooze-launcher-grid {"
    "  background: @view_bg_color;"
    "}"
    ".ooze-launch .ooze-search-entry {"
    "  min-width: 120px;"
    "  min-height: 32px;"
    "  border-radius: 10px;"
    "}"
    ".ooze-launch .ooze-launcher-grid > flowboxchild {"
    "  border-radius: 5px;"
    "  outline: none;"
    "  background: transparent;"
    "}"
    ".ooze-launch .ooze-launcher-grid > flowboxchild:hover {"
    "  background: rgba(41,104,200,0.10);"
    "}"
    ".ooze-launch .ooze-launcher-grid > flowboxchild:selected {"
    "  background: #2968c8;"
    "}"
    ".ooze-launch .ooze-launcher-grid > flowboxchild:selected .ooze-button-label {"
    "  color: #ffffff;"
    "}"
    ".ooze-launch .ooze-launcher-tile .ooze-button-label {"
    "  color: @window_fg_color;"
    "}"
    ".ooze-launch .ooze-launch-groups {"
    "  padding: 4px 10px;"
    "  background: transparent;"
    "}"
    ".ooze-launch .ooze-launch-groups > .ooze-button {"
    "  border-radius: 8px;"
    "}"
    ".ooze-launch .ooze-launch-groups > .ooze-button.active {"
    "  background: rgba(41,104,200,0.14);"
    "  color: #2968c8;"
    "}"
    ".ooze-launch .ooze-launch-groups > .ooze-button.active:hover {"
    "  background: rgba(41,104,200,0.20);"
    "}");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static void
launch_app_free (gpointer data)
{
  LaunchApp *app = data;

  g_clear_object (&app->info);
  g_free (app->id);
  g_free (app->name);
  g_free (app->search_text);
  g_free (app);
}

static void
launch_group_free (gpointer data)
{
  LaunchGroup *group = data;

  g_free (group->name);
  g_ptr_array_free (group->app_ids, TRUE);
  g_free (group);
}

static LaunchApp *
launch_find_app (OozeLaunchWindow *self,
                 const char       *id)
{
  gsize i;

  for (i = 0; i < self->apps->len; i++)
    {
      LaunchApp *app = g_ptr_array_index (self->apps, i);

      if (g_strcmp0 (app->id, id) == 0)
        return app;
    }

  return NULL;
}

static LaunchGroup *
launch_find_group (OozeLaunchWindow *self,
                   const char       *name,
                   gboolean          custom_only)
{
  gsize i;

  for (i = 0; i < self->groups->len; i++)
    {
      LaunchGroup *group = g_ptr_array_index (self->groups, i);

      if ((!custom_only || group->custom) &&
          g_strcmp0 (group->name, name) == 0)
        return group;
    }

  return NULL;
}

static gboolean
launch_group_contains (LaunchGroup *group,
                       const char  *app_id)
{
  gsize i;

  for (i = 0; i < group->app_ids->len; i++)
    {
      if (g_strcmp0 (g_ptr_array_index (group->app_ids, i), app_id) == 0)
        return TRUE;
    }

  return FALSE;
}

static void
launch_group_add_app (LaunchGroup *group,
                      const char  *app_id)
{
  if (!launch_group_contains (group, app_id))
    g_ptr_array_add (group->app_ids, g_strdup (app_id));
}

static void
launch_group_remove_app (LaunchGroup *group,
                         const char  *app_id)
{
  gsize i;

  for (i = 0; i < group->app_ids->len; i++)
    {
      if (g_strcmp0 (g_ptr_array_index (group->app_ids, i), app_id) == 0)
        {
          g_ptr_array_remove_index (group->app_ids, i);
          return;
        }
    }
}

static char *
launch_groups_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "ooze",
                           "launch-groups.conf", NULL);
}

static void
launch_save_groups (OozeLaunchWindow *self)
{
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  g_autofree char *data = NULL;
  g_autofree char *path = launch_groups_path ();
  g_autofree char *directory = g_path_get_dirname (path);
  g_autoptr (GError) error = NULL;
  gsize i;

  g_mkdir_with_parents (directory, 0700);
  for (i = 0; i < self->groups->len; i++)
    {
      LaunchGroup *group = g_ptr_array_index (self->groups, i);
      g_autofree char *section = NULL;

      if (!group->custom)
        continue;

      section = g_strdup_printf ("Group/%s", group->name);
      if (group->app_ids->len == 0)
        {
          g_key_file_set_string (key_file, section, "apps", "");
        }
      else
        {
          g_key_file_set_string_list (
            key_file, section, "apps",
            (const char * const *) group->app_ids->pdata,
            group->app_ids->len);
        }
    }

  data = g_key_file_to_data (key_file, NULL, &error);
  if (!data ||
      !g_file_set_contents (path, data, -1, &error))
    g_warning ("Ooze Launch: could not save groups: %s",
               error ? error->message : "unknown error");
}

static void
launch_load_groups (OozeLaunchWindow *self)
{
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  g_autofree char *path = launch_groups_path ();
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) sections = NULL;
  gsize n_sections;
  gsize i;

  if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error))
    return;

  sections = g_key_file_get_groups (key_file, &n_sections);
  for (i = 0; i < n_sections; i++)
    {
      const char *section = sections[i];
      g_auto (GStrv) ids = NULL;
      gsize n_ids = 0;
      LaunchGroup *group;
      gsize j;

      if (!g_str_has_prefix (section, "Group/") ||
          !section[6] ||
          launch_find_group (self, section + 6, TRUE))
        continue;

      group = g_new0 (LaunchGroup, 1);
      group->name = g_strdup (section + 6);
      group->custom = TRUE;
      group->app_ids = g_ptr_array_new_with_free_func (g_free);
      ids = g_key_file_get_string_list (key_file, section, "apps",
                                         &n_ids, NULL);
      for (j = 0; ids && j < n_ids; j++)
        if (launch_find_app (self, ids[j]))
          launch_group_add_app (group, ids[j]);
      g_ptr_array_add (self->groups, group);
    }
}

static const char *
launch_category_group_name (const char *category)
{
  if (g_strcmp0 (category, "AudioVideo") == 0 ||
      g_strcmp0 (category, "Video") == 0 ||
      g_strcmp0 (category, "Audio") == 0)
    return "Multimedia";
  if (g_strcmp0 (category, "Office") == 0)
    return "Office";
  if (g_strcmp0 (category, "Development") == 0)
    return "Development";
  if (g_strcmp0 (category, "Graphics") == 0)
    return "Graphics";
  if (g_strcmp0 (category, "Network") == 0)
    return "Internet";
  if (g_strcmp0 (category, "Game") == 0)
    return "Games";
  if (g_strcmp0 (category, "System") == 0)
    return "System";
  if (g_strcmp0 (category, "Settings") == 0)
    return "Settings";
  if (g_strcmp0 (category, "Utility") == 0)
    return "Utilities";
  return NULL;
}

static void
launch_add_auto_groups (OozeLaunchWindow *self,
                        LaunchApp        *app)
{
  const char *categories;
  g_auto (GStrv) split = NULL;
  gsize i;

  if (!G_IS_DESKTOP_APP_INFO (app->info))
    return;

  categories = g_desktop_app_info_get_categories (
    G_DESKTOP_APP_INFO (app->info));
  if (!categories || !*categories)
    return;

  split = g_strsplit (categories, ";", -1);
  for (i = 0; split[i]; i++)
    {
      const char *name = launch_category_group_name (split[i]);
      LaunchGroup *group;

      if (!name)
        continue;
      group = launch_find_group (self, name, FALSE);
      if (!group)
        {
          group = g_new0 (LaunchGroup, 1);
          group->name = g_strdup (name);
          group->app_ids = g_ptr_array_new_with_free_func (g_free);
          g_ptr_array_add (self->groups, group);
        }
      launch_group_add_app (group, app->id);
    }
}

static gint
launch_compare_apps (gconstpointer a,
                     gconstpointer b)
{
  const LaunchApp *app_a = *(LaunchApp * const *) a;
  const LaunchApp *app_b = *(LaunchApp * const *) b;

  return g_utf8_collate (app_a->name, app_b->name);
}

static void
launch_load_apps (OozeLaunchWindow *self)
{
  GList *all;
  GHashTable *seen;
  GList *l;

  all = g_app_info_get_all ();
  seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  for (l = all; l; l = l->next)
    {
      GAppInfo *info = l->data;
      const char *id;
      const char *name;
      const char *generic = NULL;
      LaunchApp *app;
      g_autofree char *search_text = NULL;

      if (!g_app_info_should_show (info))
        continue;
      id = g_app_info_get_id (info);
      name = g_app_info_get_display_name (info);
      if (!id || !*id || !name || !*name ||
          g_hash_table_contains (seen, id))
        continue;

      if (G_IS_DESKTOP_APP_INFO (info))
        generic = g_desktop_app_info_get_generic_name (
          G_DESKTOP_APP_INFO (info));
      search_text = g_utf8_casefold (name, -1);
      if (generic && *generic)
        {
          g_autofree char *generic_folded = g_utf8_casefold (generic, -1);
          char *combined = g_strdup_printf ("%s %s", search_text,
                                            generic_folded);
          g_free (search_text);
          search_text = combined;
        }

      app = g_new0 (LaunchApp, 1);
      app->info = g_object_ref (info);
      app->id = g_strdup (id);
      app->name = g_strdup (name);
      app->search_text = g_steal_pointer (&search_text);
      g_ptr_array_add (self->apps, app);
      g_hash_table_add (seen, g_strdup (id));
    }
  g_hash_table_unref (seen);
  g_list_free_full (all, g_object_unref);
  g_ptr_array_sort (self->apps, launch_compare_apps);
}

static GtkWidget *
launch_app_icon (LaunchApp *app)
{
  GIcon *icon = g_app_info_get_icon (app->info);
  GtkWidget *image;

  if (!icon)
    return ooze_icon_image_new (launch_generic_icons,
                                OOZE_ICON_SIZE_GRID);

  if (G_IS_THEMED_ICON (icon))
    {
      const char * const *names = g_themed_icon_get_names (
        G_THEMED_ICON (icon));
      gsize n_names = 0;
      const char * const *p;
      const char **with_fallback;

      for (p = names; p && *p; p++)
        n_names++;
      with_fallback = g_new0 (const char *, n_names + 3);
      for (gsize i = 0; i < n_names; i++)
        with_fallback[i] = names[i];
      with_fallback[n_names] = launch_generic_icons[0];
      with_fallback[n_names + 1] = launch_generic_icons[1];
      image = ooze_icon_image_new (with_fallback, OOZE_ICON_SIZE_GRID);
      g_free (with_fallback);
      return image;
    }

  image = gtk_image_new_from_gicon (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (image), OOZE_ICON_SIZE_GRID);
  gtk_widget_set_size_request (image, OOZE_ICON_SIZE_GRID,
                               OOZE_ICON_SIZE_GRID);
  gtk_widget_add_css_class (image, "ooze-icon");
  return image;
}

static gboolean
launch_app_matches_search (LaunchApp    *app,
                           const char   *query)
{
  g_autofree char *folded = NULL;

  if (!query || !*query)
    return TRUE;
  folded = g_utf8_casefold (query, -1);
  return strstr (app->search_text, folded) != NULL;
}

static gboolean
launch_filter_child (GtkFlowBoxChild *child,
                     gpointer         user_data)
{
  OozeLaunchWindow *self = user_data;
  GtkWidget *tile = gtk_flow_box_child_get_child (child);
  LaunchApp *app = g_object_get_data (G_OBJECT (tile), "launch-app");
  const char *query = gtk_editable_get_text (GTK_EDITABLE (self->search));

  if (!launch_app_matches_search (app, query))
    return FALSE;
  if (query && *query)
    return TRUE;
  return !self->active_group ||
         launch_group_contains (self->active_group, app->id);
}

static void
launch_close_context_popover (OozeLaunchWindow *self)
{
  if (!self->context_popover)
    return;

  gtk_popover_popdown (GTK_POPOVER (self->context_popover));
  gtk_widget_unparent (self->context_popover);
  g_clear_object (&self->context_popover);
}

typedef struct
{
  OozeLaunchWindow *window;
  LaunchApp        *app;
  LaunchGroup      *group;
  gboolean          remove;
} LaunchAssignment;

static void
launch_assignment_free (gpointer data,
                        GClosure *closure G_GNUC_UNUSED)
{
  g_free (data);
}

static void
launch_assign_clicked (GtkButton *button G_GNUC_UNUSED,
                       gpointer   user_data)
{
  LaunchAssignment *assignment = user_data;

  if (assignment->remove)
    launch_group_remove_app (assignment->group, assignment->app->id);
  else
    launch_group_add_app (assignment->group, assignment->app->id);
  launch_save_groups (assignment->window);
  launch_refresh_grid (assignment->window);
  launch_close_context_popover (assignment->window);
}

static void
launch_delete_group_clicked (GtkButton *button G_GNUC_UNUSED,
                             gpointer   user_data)
{
  OozeLaunchWindow *self = user_data;
  LaunchGroup *group = self->active_group;

  if (!group || !group->custom)
    return;
  self->active_group = NULL;
  g_ptr_array_remove (self->groups, group);
  launch_save_groups (self);
  launch_rebuild_groups_bar (self);
  launch_refresh_grid (self);
}

static void
launch_show_context_menu (OozeLaunchWindow *self,
                          LaunchApp        *app,
                          GtkWidget        *anchor,
                          double            x,
                          double            y)
{
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *popover;
  GdkRectangle rectangle;
  gsize i;

  launch_close_context_popover (self);
  popover = gtk_popover_new ();
  self->context_popover = g_object_ref_sink (popover);
  gtk_widget_set_parent (popover, anchor);
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), TRUE);
  rectangle = (GdkRectangle) { (int) x, (int) y, 1, 1 };
  gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rectangle);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start (box, 8);
  gtk_widget_set_margin_end (box, 8);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_popover_set_child (GTK_POPOVER (popover), box);
  label = gtk_label_new (app->name);
  gtk_widget_add_css_class (label, "heading");
  gtk_box_append (GTK_BOX (box), label);

  for (i = 0; i < self->groups->len; i++)
    {
      LaunchGroup *group = g_ptr_array_index (self->groups, i);
      GtkWidget *button;
      LaunchAssignment *assignment;
      g_autofree char *label = NULL;

      if (!group->custom ||
          (group == self->active_group &&
           launch_group_contains (group, app->id)))
        continue;
      if (launch_group_contains (group, app->id))
        label = g_strdup ("Remove from group");
      else
        label = g_strdup_printf ("Add to %s", group->name);
      button = gtk_button_new_with_label (label);
      gtk_widget_add_css_class (button, "ooze-menu-item");
      assignment = g_new0 (LaunchAssignment, 1);
      assignment->window = self;
      assignment->app = app;
      assignment->group = group;
      assignment->remove = launch_group_contains (group, app->id);
      g_signal_connect_data (button, "clicked",
                             G_CALLBACK (launch_assign_clicked),
                             assignment, launch_assignment_free, 0);
      gtk_box_append (GTK_BOX (box), button);
    }

  if (self->active_group && self->active_group->custom &&
      launch_group_contains (self->active_group, app->id))
    {
      GtkWidget *button = gtk_button_new_with_label ("Remove from this group");
      LaunchAssignment *assignment = g_new0 (LaunchAssignment, 1);

      gtk_widget_add_css_class (button, "ooze-menu-item");
      assignment->window = self;
      assignment->app = app;
      assignment->group = self->active_group;
      assignment->remove = TRUE;
      g_signal_connect_data (button, "clicked",
                             G_CALLBACK (launch_assign_clicked),
                             assignment, launch_assignment_free, 0);
      gtk_box_append (GTK_BOX (box), button);
    }

  if (self->active_group && self->active_group->custom)
    {
      GtkWidget *button = gtk_button_new_with_label ("Delete group");

      gtk_widget_add_css_class (button, "ooze-destructive");
      g_signal_connect (button, "clicked",
                        G_CALLBACK (launch_delete_group_clicked), self);
      gtk_box_append (GTK_BOX (box), button);
    }

  gtk_popover_popup (GTK_POPOVER (popover));
}

static void
launch_tile_pressed (GtkGestureClick *gesture,
                     int             n_press G_GNUC_UNUSED,
                     double          x,
                     double          y,
                     gpointer        user_data)
{
  GtkWidget *tile = gtk_event_controller_get_widget (
    GTK_EVENT_CONTROLLER (gesture));
  OozeLaunchWindow *self = user_data;
  LaunchApp *app = g_object_get_data (G_OBJECT (tile), "launch-app");

  if (gtk_gesture_single_get_current_button (
        GTK_GESTURE_SINGLE (gesture)) == GDK_BUTTON_SECONDARY)
    launch_show_context_menu (self, app, tile, x, y);
}

static void
launch_app_clicked (GtkButton *button,
                    gpointer   user_data)
{
  OozeLaunchWindow *self = user_data;
  LaunchApp *app = g_object_get_data (G_OBJECT (button), "launch-app");
  g_autoptr (GAppLaunchContext) context = g_app_launch_context_new ();
  g_autoptr (GError) error = NULL;

  ooze_appmenu_prepare_launch_context (context);
  if (!g_app_info_launch (app->info, NULL, context, &error))
    {
      g_warning ("Ooze Launch: failed to launch %s: %s",
                 app->name, error ? error->message : "unknown error");
      return;
    }

  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
}

static GtkWidget *
launch_make_tile (OozeLaunchWindow *self,
                  LaunchApp        *app)
{
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *label;
  GtkGesture *gesture;

  button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  gtk_widget_add_css_class (button, "ooze-launcher-tile");
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_box_append (GTK_BOX (box), launch_app_icon (app));
  label = gtk_label_new (app->name);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 14);
  gtk_widget_add_css_class (label, "ooze-button-label");
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), label);
  gtk_widget_set_tooltip_text (button, app->name);
  g_object_set_data (G_OBJECT (button), "launch-app", app);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (launch_app_clicked), self);

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture),
                                 GDK_BUTTON_SECONDARY);
  g_signal_connect (gesture, "pressed",
                    G_CALLBACK (launch_tile_pressed), self);
  gtk_widget_add_controller (button, GTK_EVENT_CONTROLLER (gesture));
  return button;
}

static void
launch_refresh_grid (OozeLaunchWindow *self)
{
  gtk_flow_box_invalidate_filter (GTK_FLOW_BOX (self->grid));
}

static void
launch_search_changed (GtkEditable      *editable G_GNUC_UNUSED,
                       GParamSpec       *pspec G_GNUC_UNUSED,
                       OozeLaunchWindow *self)
{
  launch_refresh_grid (self);
}

static void
launch_group_clicked (GtkButton *button,
                      gpointer   user_data)
{
  OozeLaunchWindow *self = user_data;
  LaunchGroup *group = g_object_get_data (G_OBJECT (button), "launch-group");

  self->active_group = group;
  launch_rebuild_groups_bar (self);
  launch_refresh_grid (self);
}

static void
launch_home_clicked (GtkButton *button G_GNUC_UNUSED,
                     gpointer   user_data)
{
  OozeLaunchWindow *self = user_data;

  self->active_group = NULL;
  launch_rebuild_groups_bar (self);
  launch_refresh_grid (self);
}

static void
launch_add_group_close (GtkPopover *popover,
                        gpointer    user_data)
{
  OozeLaunchWindow *self = user_data;

  gtk_widget_unparent (GTK_WIDGET (popover));
  g_clear_object (&self->add_popover);
  self->add_entry = NULL;
}

static void
launch_add_group_submit (GtkButton *button G_GNUC_UNUSED,
                         gpointer   user_data)
{
  OozeLaunchWindow *self = user_data;
  const char *name = gtk_editable_get_text (
    GTK_EDITABLE (self->add_entry));
  LaunchGroup *group;

  if (!name || !*name || launch_find_group (self, name, TRUE))
    return;

  group = g_new0 (LaunchGroup, 1);
  group->name = g_strdup (name);
  group->custom = TRUE;
  group->app_ids = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (self->groups, group);
  launch_save_groups (self);
  gtk_popover_popdown (GTK_POPOVER (self->add_popover));
  launch_rebuild_groups_bar (self);
}

static void
launch_add_group_clicked (GtkButton         *button,
                          gpointer           user_data)
{
  OozeLaunchWindow *self = user_data;
  GtkWidget *box;
  GtkWidget *submit;
  GtkWidget *popover;

  if (self->add_popover)
    {
      gtk_popover_popup (GTK_POPOVER (self->add_popover));
      return;
    }

  popover = gtk_popover_new ();
  self->add_popover = g_object_ref_sink (popover);
  gtk_widget_set_parent (popover, GTK_WIDGET (button));
  gtk_popover_set_autohide (GTK_POPOVER (popover), TRUE);
  g_signal_connect (popover, "closed",
                    G_CALLBACK (launch_add_group_close), self);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_popover_set_child (GTK_POPOVER (popover), box);
  self->add_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->add_entry), "Group name");
  gtk_box_append (GTK_BOX (box), self->add_entry);
  submit = gtk_button_new_with_label ("Add group");
  gtk_widget_add_css_class (submit, "ooze-menu-item");
  g_signal_connect (submit, "clicked",
                    G_CALLBACK (launch_add_group_submit), self);
  gtk_box_append (GTK_BOX (box), submit);
  gtk_popover_popup (GTK_POPOVER (popover));
  gtk_widget_grab_focus (self->add_entry);
}

static void
launch_rebuild_groups_bar (OozeLaunchWindow *self)
{
  GtkWidget *child;
  GtkWidget *button;
  const char * const home_icons[] = { "go-home", NULL };
  const char * const group_icons[] = { "folder", NULL };
  const char * const add_icons[] = { "list-add", NULL };
  gsize i;
  GPtrArray *peers = g_ptr_array_new ();

  while ((child = gtk_widget_get_first_child (self->groups_bar)) != NULL)
    gtk_box_remove (GTK_BOX (self->groups_bar), child);

  button = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR, home_icons, 24,
                                    "Library Home", "Show all applications");
  g_signal_connect (button, "clicked",
                    G_CALLBACK (launch_home_clicked), self);
  gtk_box_append (GTK_BOX (self->groups_bar), button);
  g_ptr_array_add (peers, button);

  for (i = 0; i < self->groups->len; i++)
    {
      LaunchGroup *group = g_ptr_array_index (self->groups, i);

      if (!group->custom && group->app_ids->len == 0)
        continue;
      button = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR, group_icons, 24,
                                        group->name, group->name);
      group->button = button;
      g_object_set_data (G_OBJECT (button), "launch-group", group);
      g_signal_connect (button, "clicked",
                        G_CALLBACK (launch_group_clicked), self);
      gtk_box_append (GTK_BOX (self->groups_bar), button);
      g_ptr_array_add (peers, button);
    }

  button = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR, add_icons, 24,
                                    "Add group", "Create a custom group");
  g_signal_connect (button, "clicked",
                    G_CALLBACK (launch_add_group_clicked), self);
  gtk_box_append (GTK_BOX (self->groups_bar), button);

  if (!self->active_group)
    ooze_button_set_exclusive ((GtkWidget **) peers->pdata, peers->len, 0);
  else
    {
      for (i = 0; i < peers->len; i++)
        {
          LaunchGroup *group = g_object_get_data (
            G_OBJECT (g_ptr_array_index (peers, i)), "launch-group");
          if (group == self->active_group)
            ooze_button_set_exclusive ((GtkWidget **) peers->pdata,
                                       peers->len, i);
        }
    }
  g_ptr_array_free (peers, TRUE);
}

static void
ooze_launch_window_constructed (GObject *object)
{
  OozeLaunchWindow *self = OOZE_LAUNCH_WINDOW (object);
  GtkWidget *shell;
  GtkWidget *header;
  GtkWidget *statusbar;
  GtkWidget *scroll;
  GtkWidget *search_box;
  gsize i;

  G_OBJECT_CLASS (ooze_launch_window_parent_class)->constructed (object);
  ooze_toolbar_ensure_css ();
  ooze_scroll_ensure_css ();
  ooze_launch_ensure_css ();

  self->apps = g_ptr_array_new_with_free_func (launch_app_free);
  self->groups = g_ptr_array_new_with_free_func (launch_group_free);
  launch_load_apps (self);
  for (i = 0; i < self->apps->len; i++)
    launch_add_auto_groups (self, g_ptr_array_index (self->apps, i));
  launch_load_groups (self);

  gtk_window_set_default_size (GTK_WINDOW (self), 720, 560);
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Launch");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-launch");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Applications");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  header = ooze_surface_new (OOZE_SURFACE_TOOLBAR,
                             GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (header, "ooze-launch-header");
  gtk_widget_set_hexpand (header, TRUE);
  gtk_box_append (GTK_BOX (shell), header);

  search_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_start (search_box, 10);
  gtk_widget_set_margin_end (search_box, 10);
  gtk_widget_set_margin_top (search_box, 5);
  gtk_widget_set_margin_bottom (search_box, 5);
  self->search = gtk_search_entry_new ();
  gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (self->search),
                                         "Type to search apps…");
  gtk_widget_add_css_class (self->search, "ooze-search-entry");
  gtk_widget_add_css_class (self->search, "ooze-toolbar-search");
  gtk_widget_set_hexpand (self->search, TRUE);
  gtk_box_append (GTK_BOX (search_box), self->search);
  gtk_box_append (GTK_BOX (header), search_box);
  g_signal_connect (self->search, "notify::text",
                    G_CALLBACK (launch_search_changed), self);

  scroll = ooze_scrolled_window_new ();
  gtk_widget_add_css_class (scroll, "ooze-launch-grid-scroll");
  gtk_widget_set_hexpand (scroll, TRUE);
  gtk_widget_set_vexpand (scroll, TRUE);
  self->grid = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid),
                                   GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid), TRUE);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->grid), 5);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->grid), 6);
  gtk_widget_add_css_class (self->grid, "ooze-launcher-grid");
  gtk_widget_set_hexpand (self->grid, TRUE);
  gtk_widget_set_vexpand (self->grid, TRUE);
  gtk_flow_box_set_filter_func (GTK_FLOW_BOX (self->grid),
                                launch_filter_child, self, NULL);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), self->grid);
  gtk_box_append (GTK_BOX (shell), scroll);

  statusbar = ooze_surface_new (OOZE_SURFACE_STATUSBAR,
                                GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (statusbar, "ooze-launch-statusbar");
  gtk_widget_set_hexpand (statusbar, TRUE);
  gtk_box_append (GTK_BOX (shell), statusbar);

  self->groups_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_hexpand (self->groups_bar, TRUE);
  gtk_widget_add_css_class (self->groups_bar, "ooze-launch-groups");
  gtk_box_append (GTK_BOX (statusbar), self->groups_bar);

  for (i = 0; i < self->apps->len; i++)
    {
      LaunchApp *app = g_ptr_array_index (self->apps, i);
      app->tile = launch_make_tile (self, app);
      gtk_flow_box_append (GTK_FLOW_BOX (self->grid), app->tile);
    }

  launch_rebuild_groups_bar (self);
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);
}

static void
ooze_launch_window_dispose (GObject *object)
{
  OozeLaunchWindow *self = OOZE_LAUNCH_WINDOW (object);

  launch_close_context_popover (self);
  if (self->add_popover)
    {
      gtk_popover_popdown (GTK_POPOVER (self->add_popover));
      gtk_widget_unparent (self->add_popover);
      g_clear_object (&self->add_popover);
    }
  g_clear_pointer (&self->apps, g_ptr_array_unref);
  g_clear_pointer (&self->groups, g_ptr_array_unref);
  G_OBJECT_CLASS (ooze_launch_window_parent_class)->dispose (object);
}

static void
ooze_launch_window_class_init (OozeLaunchWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_launch_window_constructed;
  object_class->dispose = ooze_launch_window_dispose;
}

static void
ooze_launch_window_init (OozeLaunchWindow *self G_GNUC_UNUSED)
{
}

GtkWidget *
ooze_launch_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_LAUNCH_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
