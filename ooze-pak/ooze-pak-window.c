#include "ooze-pak-window.h"
#include "ooze-pak-flatpak.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-icons.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <string.h>

#define PAK_MIME_UNINSTALL "application/x-ooze-pak-uninstall"

struct _OozePakWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *header;
  GtkWidget *flow;
  GtkWidget *scrolled;
  GtkWidget *status;
  GtkWidget *trash_strip;
  GtkWidget *empty_label;
  GtkWidget *busy;

  guint busy_count;
};

G_DEFINE_FINAL_TYPE (OozePakWindow, ooze_pak_window, GTK_TYPE_APPLICATION_WINDOW)

static void ooze_pak_window_reload (OozePakWindow *self);
static void ooze_pak_confirm_uninstall (OozePakWindow *self,
                                        const char    *app_id,
                                        const char    *name,
                                        const char    *installation);

static void
set_status (OozePakWindow *self, const char *text)
{
  gtk_label_set_text (GTK_LABEL (self->status), text ? text : "");
}

static void
set_busy (OozePakWindow *self, gboolean on)
{
  if (on)
    self->busy_count++;
  else if (self->busy_count > 0)
    self->busy_count--;

  gtk_widget_set_visible (self->busy, self->busy_count > 0);
  gtk_widget_set_sensitive (self->flow, self->busy_count == 0);
  gtk_widget_set_sensitive (self->trash_strip, self->busy_count == 0);
}

static void
clear_flow (OozePakWindow *self)
{
  GtkWidget *child = gtk_widget_get_first_child (self->flow);

  while (child != NULL)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);
      if (GTK_IS_FLOW_BOX_CHILD (child))
        gtk_flow_box_remove (GTK_FLOW_BOX (self->flow), child);
      child = next;
    }
}

static GdkContentProvider *
pak_drag_prepare (GtkDragSource *source G_GNUC_UNUSED,
                  double         x G_GNUC_UNUSED,
                  double         y G_GNUC_UNUSED,
                  gpointer       user_data)
{
  GtkWidget *cell = user_data;
  const char *app_id = g_object_get_data (G_OBJECT (cell), "app-id");
  g_autoptr (GdkContentProvider) plain = NULL;
  g_autoptr (GdkContentProvider) custom = NULL;
  g_autoptr (GBytes) bytes = NULL;

  if (!app_id)
    return NULL;

  plain = gdk_content_provider_new_typed (G_TYPE_STRING, app_id);
  bytes = g_bytes_new (app_id, strlen (app_id));
  custom = gdk_content_provider_new_for_bytes (PAK_MIME_UNINSTALL, bytes);

  return gdk_content_provider_new_union ((GdkContentProvider *[2]) {
                                           g_steal_pointer (&custom),
                                           g_steal_pointer (&plain),
                                         },
                                         2);
}

static void
on_cell_drag_begin (GtkDragSource *source G_GNUC_UNUSED,
                    GdkDrag       *drag G_GNUC_UNUSED,
                    gpointer       user_data)
{
  GtkWidget *cell = user_data;
  OozePakWindow *self = g_object_get_data (G_OBJECT (cell), "pak-window");
  if (self)
    gtk_widget_add_css_class (self->trash_strip, "drop-ready");
}

static void
on_cell_drag_end (GtkDragSource *source G_GNUC_UNUSED,
                  GdkDrag       *drag G_GNUC_UNUSED,
                  gboolean       delete_data G_GNUC_UNUSED,
                  gpointer       user_data)
{
  GtkWidget *cell = user_data;
  OozePakWindow *self = g_object_get_data (G_OBJECT (cell), "pak-window");
  if (self)
    gtk_widget_remove_css_class (self->trash_strip, "drop-ready");
}

static void
on_uninstall_done (gboolean   ok,
                   const char *message,
                   gpointer    user_data)
{
  OozePakWindow *self = user_data;

  set_busy (self, FALSE);
  set_status (self, message);
  if (ok)
    ooze_pak_window_reload (self);
}

static void
on_install_done (gboolean   ok,
                 const char *message,
                 gpointer    user_data)
{
  OozePakWindow *self = user_data;

  set_busy (self, FALSE);
  set_status (self, message);
  if (ok)
    ooze_pak_window_reload (self);
}

static void
on_uninstall_response (AdwAlertDialog *dialog,
                       GAsyncResult   *result,
                       gpointer        user_data)
{
  OozePakWindow *self = user_data;
  const char *app_id;
  const char *installation;
  const char *response;

  response = adw_alert_dialog_choose_finish (dialog, result);
  app_id = g_object_get_data (G_OBJECT (dialog), "app-id");
  installation = g_object_get_data (G_OBJECT (dialog), "installation");

  if (g_strcmp0 (response, "uninstall") != 0 || !app_id)
    return;

  set_busy (self, TRUE);
  set_status (self, "Uninstalling…");
  ooze_pak_flatpak_uninstall_async (app_id, installation,
                                    on_uninstall_done, self);
}

static void
ooze_pak_confirm_uninstall (OozePakWindow *self,
                            const char    *app_id,
                            const char    *name,
                            const char    *installation)
{
  AdwAlertDialog *dialog;
  g_autofree char *body = NULL;

  if (!app_id || !*app_id)
    return;

  body = g_strdup_printf ("Remove “%s” (%s) from this computer?",
                          name && *name ? name : app_id,
                          app_id);

  dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new ("Uninstall App?", body));
  adw_alert_dialog_add_responses (dialog,
                                  "cancel", "_Cancel",
                                  "uninstall", "_Uninstall",
                                  NULL);
  adw_alert_dialog_set_response_appearance (dialog, "uninstall",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (dialog, "cancel");
  adw_alert_dialog_set_close_response (dialog, "cancel");

  g_object_set_data_full (G_OBJECT (dialog), "app-id",
                          g_strdup (app_id), g_free);
  g_object_set_data_full (G_OBJECT (dialog), "installation",
                          g_strdup (installation ? installation : "user"),
                          g_free);

  adw_alert_dialog_choose (dialog, GTK_WIDGET (self), NULL,
                           (GAsyncReadyCallback) on_uninstall_response, self);
}

static void
on_uninstall_menu (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  GtkWidget *cell = user_data;
  OozePakWindow *self = g_object_get_data (G_OBJECT (cell), "pak-window");
  const char *app_id = g_object_get_data (G_OBJECT (cell), "app-id");
  const char *name = g_object_get_data (G_OBJECT (cell), "app-name");
  const char *installation = g_object_get_data (G_OBJECT (cell), "installation");

  if (self)
    ooze_pak_confirm_uninstall (self, app_id, name, installation);
}

static void
on_cell_pressed (GtkGestureClick *gesture,
                 int              n_press G_GNUC_UNUSED,
                 double           x G_GNUC_UNUSED,
                 double           y G_GNUC_UNUSED,
                 gpointer         user_data)
{
  GtkWidget *cell = user_data;
  OozePakWindow *self = g_object_get_data (G_OBJECT (cell), "pak-window");
  GtkWidget *popover;
  GMenu *menu;
  GSimpleActionGroup *group;
  GSimpleAction *act;

  if (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture)) != GDK_BUTTON_SECONDARY)
    return;

  if (!self)
    return;

  menu = g_menu_new ();
  g_menu_append (menu, "Uninstall…", "pak.uninstall");

  group = g_simple_action_group_new ();
  act = g_simple_action_new ("uninstall", NULL);
  g_signal_connect (act, "activate", G_CALLBACK (on_uninstall_menu), cell);
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (act));
  g_object_unref (act);

  popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  g_object_unref (menu);
  gtk_widget_insert_action_group (popover, "pak", G_ACTION_GROUP (group));
  g_object_unref (group);

  gtk_widget_set_parent (popover, GTK_WIDGET (self));
  gtk_popover_popup (GTK_POPOVER (popover));
  g_signal_connect (popover, "closed", G_CALLBACK (gtk_widget_unparent), NULL);
}

static GtkWidget *
create_app_cell (OozePakWindow *self, OozePakApp *app)
{
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *label;
  GtkDragSource *drag;
  GtkGestureClick *click;
  const char *icons[] = { app->app_id, "system-software-install", "package-x-generic", NULL };

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class (box, "pak-grid-cell");
  gtk_widget_set_size_request (box, 96, -1);
  gtk_widget_set_valign (box, GTK_ALIGN_START);
  gtk_widget_set_margin_top (box, 4);
  gtk_widget_set_margin_bottom (box, 4);
  gtk_widget_set_margin_start (box, 4);
  gtk_widget_set_margin_end (box, 4);
  gtk_widget_set_tooltip_text (box, app->app_id);

  image = ooze_icon_image_new (icons, OOZE_ICON_SIZE_GRID);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), image);

  label = gtk_label_new (app->name);
  gtk_widget_add_css_class (label, "pak-grid-label");
  gtk_label_set_xalign (GTK_LABEL (label), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 12);
  gtk_label_set_lines (GTK_LABEL (label), 2);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_box_append (GTK_BOX (box), label);

  g_object_set_data_full (G_OBJECT (box), "app-id", g_strdup (app->app_id), g_free);
  g_object_set_data_full (G_OBJECT (box), "app-name", g_strdup (app->name), g_free);
  g_object_set_data_full (G_OBJECT (box), "installation",
                          g_strdup (app->installation ? app->installation : "user"),
                          g_free);
  g_object_set_data (G_OBJECT (box), "pak-window", self);

  drag = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag, GDK_ACTION_MOVE | GDK_ACTION_COPY);
  g_signal_connect (drag, "prepare", G_CALLBACK (pak_drag_prepare), box);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_cell_drag_begin), box);
  g_signal_connect (drag, "drag-end", G_CALLBACK (on_cell_drag_end), box);
  gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (drag));

  click = GTK_GESTURE_CLICK (gtk_gesture_click_new ());
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  g_signal_connect (click, "pressed", G_CALLBACK (on_cell_pressed), box);
  gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (click));

  return box;
}

static void
ooze_pak_window_reload (OozePakWindow *self)
{
  g_autoptr (GError) error = NULL;
  GList *apps;
  GList *l;
  guint count = 0;

  clear_flow (self);

  if (!ooze_pak_flatpak_available ())
    {
      gtk_widget_set_visible (self->empty_label, TRUE);
      gtk_label_set_text (GTK_LABEL (self->empty_label),
                          "Flatpak is not installed.\n"
                          "Install the flatpak package, then refresh.\n\n"
                          "You can still drop .flatpak files here once it is available.");
      set_status (self, "flatpak not found");
      return;
    }

  apps = ooze_pak_flatpak_list_apps (&error);
  if (error)
    {
      gtk_widget_set_visible (self->empty_label, TRUE);
      gtk_label_set_text (GTK_LABEL (self->empty_label), error->message);
      set_status (self, error->message);
      return;
    }

  for (l = apps; l != NULL; l = l->next)
    {
      OozePakApp *app = l->data;
      GtkWidget *cell = create_app_cell (self, app);
      GtkWidget *fbc;

      gtk_flow_box_append (GTK_FLOW_BOX (self->flow), cell);
      fbc = gtk_widget_get_parent (cell);
      if (fbc)
        gtk_widget_set_valign (fbc, GTK_ALIGN_START);
      count++;
    }

  g_list_free_full (apps, (GDestroyNotify) ooze_pak_app_free);

  gtk_widget_set_visible (self->empty_label, count == 0);
  if (count == 0)
    gtk_label_set_text (GTK_LABEL (self->empty_label),
                        "No Flatpak apps installed.\n"
                        "Drop a .flatpak or .flatpakref here to install.");

  {
    g_autofree char *msg = g_strdup_printf ("%u app%s — drop packages to install, "
                                            "drag apps to the trash to remove",
                                            count, count == 1 ? "" : "s");
    set_status (self, msg);
  }
}

static gboolean
on_trash_drop (GtkDropTarget *target G_GNUC_UNUSED,
               const GValue  *value,
               double         x G_GNUC_UNUSED,
               double         y G_GNUC_UNUSED,
               gpointer       user_data)
{
  OozePakWindow *self = user_data;
  const char *app_id = NULL;

  gtk_widget_remove_css_class (self->trash_strip, "drop-hover");

  if (G_VALUE_HOLDS_STRING (value))
    app_id = g_value_get_string (value);

  if (!app_id || !*app_id)
    return FALSE;

  {
    GtkWidget *child = gtk_widget_get_first_child (self->flow);
    const char *name = app_id;
    const char *installation = "user";

    while (child)
      {
        if (GTK_IS_FLOW_BOX_CHILD (child))
          {
            GtkWidget *cell = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
            const char *id = g_object_get_data (G_OBJECT (cell), "app-id");
            if (g_strcmp0 (id, app_id) == 0)
              {
                name = g_object_get_data (G_OBJECT (cell), "app-name");
                installation = g_object_get_data (G_OBJECT (cell), "installation");
                break;
              }
          }
        child = gtk_widget_get_next_sibling (child);
      }

    ooze_pak_confirm_uninstall (self, app_id, name, installation);
  }

  return TRUE;
}

static GdkDragAction
on_trash_enter (GtkDropTarget *target G_GNUC_UNUSED,
                double         x G_GNUC_UNUSED,
                double         y G_GNUC_UNUSED,
                gpointer       user_data)
{
  OozePakWindow *self = user_data;
  gtk_widget_add_css_class (self->trash_strip, "drop-hover");
  return GDK_ACTION_MOVE;
}

static void
on_trash_leave (GtkDropTarget *target G_GNUC_UNUSED,
                gpointer       user_data)
{
  OozePakWindow *self = user_data;
  gtk_widget_remove_css_class (self->trash_strip, "drop-hover");
}

static gboolean
on_window_drop (GtkDropTarget *target G_GNUC_UNUSED,
                const GValue  *value,
                double         x G_GNUC_UNUSED,
                double         y G_GNUC_UNUSED,
                gpointer       user_data)
{
  OozePakWindow *self = user_data;
  const GSList *files;
  GPtrArray *paths;
  guint i;

  if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return FALSE;

  files = g_value_get_boxed (value);
  if (!files)
    return FALSE;

  paths = g_ptr_array_new_with_free_func (g_free);
  for (; files; files = files->next)
    {
      GFile *file = files->data;
      g_autofree char *path = g_file_get_path (file);
      if (path && ooze_pak_path_looks_installable (path))
        g_ptr_array_add (paths, g_steal_pointer (&path));
    }

  if (paths->len == 0)
    {
      g_ptr_array_unref (paths);
      set_status (self, "Drop a .flatpak or .flatpakref to install");
      return FALSE;
    }

  for (i = 0; i < paths->len; i++)
    {
      set_busy (self, TRUE);
      set_status (self, "Installing…");
      ooze_pak_flatpak_install_async (paths->pdata[i], on_install_done, self);
    }

  g_ptr_array_unref (paths);
  return TRUE;
}

static void
on_refresh_clicked (GtkButton *btn G_GNUC_UNUSED, OozePakWindow *self)
{
  ooze_pak_window_reload (self);
}

void
ooze_pak_window_install_paths (OozePakWindow *self,
                               GFile        **files,
                               int            n_files)
{
  int i;

  g_return_if_fail (OOZE_PAK_IS_WINDOW (self));

  for (i = 0; i < n_files; i++)
    {
      g_autofree char *path = g_file_get_path (files[i]);
      if (!path || !ooze_pak_path_looks_installable (path))
        continue;
      set_busy (self, TRUE);
      set_status (self, "Installing…");
      ooze_pak_flatpak_install_async (path, on_install_done, self);
    }
}

void
ooze_pak_window_uninstall_app (OozePakWindow *self,
                               const char    *app_id)
{
  g_return_if_fail (OOZE_PAK_IS_WINDOW (self));
  ooze_pak_confirm_uninstall (self, app_id, app_id, "user");
}

static void
pak_action_about (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *param G_GNUC_UNUSED,
                  gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Pak",
                      "system-software-install",
                      "Software installer for Ooze Desktop.");
}

static GMenuModel *
pak_build_menubar (void)
{
  GMenu *bar, *help;
  GMenuItem *item;

  bar = g_menu_new ();
  help = g_menu_new ();
  g_menu_append (help, "About Ooze Pak", "win.about");
  item = g_menu_item_new_submenu ("Help", G_MENU_MODEL (help));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (help);
  return G_MENU_MODEL (bar);
}

static void
ooze_pak_window_class_init (OozePakWindowClass *klass G_GNUC_UNUSED)
{
}

static void
ooze_pak_window_init (OozePakWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", pak_action_about, NULL, NULL, NULL },
  };
  GtkWidget *shell;
  GtkWidget *surface;
  GtkWidget *toolbar;
  GtkWidget *overlay;
  GtkWidget *trash_box;
  GtkWidget *trash_icon;
  GtkWidget *trash_label;
  GtkDropTarget *win_drop;
  GtkDropTarget *trash_drop;
  g_autoptr (GtkCssProvider) css = NULL;
  const char *refresh_icons[] = { "view-refresh", "view-refresh-symbolic", NULL };
  const char *trash_icons[] = { "user-trash", "user-trash-symbolic", "edit-delete", NULL };

  ooze_toolbar_ensure_css ();

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);

  css = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
      css,
      ".pak-grid-view { padding: 12px; }"
      ".pak-grid-label { font-size: 11pt; }"
      ".pak-empty {"
      "  opacity: 0.7;"
      "  margin: 48px;"
      "}"
      ".pak-trash-strip {"
      "  min-height: 56px;"
      "  padding: 8px;"
      "  border-top: 1px solid alpha(@borders, 0.6);"
      "}"
      ".pak-trash-strip.drop-ready {"
      "  background: alpha(#c44, 0.12);"
      "}"
      ".pak-trash-strip.drop-hover {"
      "  background: alpha(#c44, 0.28);"
      "}"
      ".pak-status { padding: 4px 10px 8px 10px; }");
  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (),
      GTK_STYLE_PROVIDER (css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_window_set_default_size (GTK_WINDOW (self), 720, 520);
  gtk_window_set_title (GTK_WINDOW (self), "Software");
  gtk_window_set_icon_name (GTK_WINDOW (self), "system-software-install");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-pak");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "Software");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  toolbar = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (toolbar, "spot-toolbar");
  {
    GtkWidget *btn = ooze_button_new_toolbar (refresh_icons, "Refresh", "Refresh");
    g_signal_connect (btn, "clicked", G_CALLBACK (on_refresh_clicked), self);
    gtk_box_append (GTK_BOX (toolbar), btn);
  }
  gtk_box_append (GTK_BOX (shell), toolbar);

  surface = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_box_append (GTK_BOX (shell), surface);

  overlay = gtk_overlay_new ();
  gtk_widget_set_hexpand (overlay, TRUE);
  gtk_widget_set_vexpand (overlay, TRUE);
  gtk_box_append (GTK_BOX (surface), overlay);

  self->scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->scrolled, TRUE);
  gtk_widget_set_vexpand (self->scrolled, TRUE);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), self->scrolled);

  self->flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->flow), 12);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->flow), 2);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->flow), TRUE);
  gtk_widget_add_css_class (self->flow, "pak-grid-view");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled), self->flow);

  self->empty_label = gtk_label_new (NULL);
  gtk_label_set_justify (GTK_LABEL (self->empty_label), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (self->empty_label), TRUE);
  gtk_widget_add_css_class (self->empty_label, "pak-empty");
  gtk_widget_set_halign (self->empty_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->empty_label, GTK_ALIGN_CENTER);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), self->empty_label);

  self->busy = gtk_spinner_new ();
  gtk_widget_set_halign (self->busy, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->busy, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->busy, FALSE);
  gtk_spinner_start (GTK_SPINNER (self->busy));
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), self->busy);

  /* Trash strip — drop apps here to uninstall (Finder-style). */
  self->trash_strip = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (self->trash_strip, "pak-trash-strip");
  trash_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign (trash_box, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (trash_box, TRUE);
  trash_icon = ooze_icon_image_new (trash_icons, OOZE_ICON_SIZE_TOOLBAR);
  trash_label = gtk_label_new ("Drag apps here to uninstall");
  gtk_box_append (GTK_BOX (trash_box), trash_icon);
  gtk_box_append (GTK_BOX (trash_box), trash_label);
  gtk_box_append (GTK_BOX (self->trash_strip), trash_box);
  gtk_box_append (GTK_BOX (shell), self->trash_strip);

  self->status = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->status), 0.0);
  gtk_widget_add_css_class (self->status, "pak-status");
  {
    GtkWidget *status_bar = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append (GTK_BOX (status_bar), self->status);
    gtk_box_append (GTK_BOX (shell), status_bar);
  }

  gtk_window_set_child (GTK_WINDOW (self), shell);

  /* Window accepts package files to install. */
  win_drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect (win_drop, "drop", G_CALLBACK (on_window_drop), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (win_drop));

  /* Trash accepts app-id strings from our drag source. */
  trash_drop = gtk_drop_target_new (G_TYPE_STRING, GDK_ACTION_MOVE | GDK_ACTION_COPY);
  g_signal_connect (trash_drop, "drop", G_CALLBACK (on_trash_drop), self);
  g_signal_connect (trash_drop, "enter", G_CALLBACK (on_trash_enter), self);
  g_signal_connect (trash_drop, "leave", G_CALLBACK (on_trash_leave), self);
  gtk_widget_add_controller (self->trash_strip, GTK_EVENT_CONTROLLER (trash_drop));

  ooze_pak_window_reload (self);
}

OozePakWindow *
ooze_pak_window_new (AdwApplication *app)
{
  OozePakWindow *win;
  GMenuModel *menubar;

  win = g_object_new (OOZE_PAK_TYPE_WINDOW, "application", app, NULL);

  menubar = pak_build_menubar ();
  gtk_application_set_menubar (GTK_APPLICATION (app), menubar);
  g_object_unref (menubar);
  gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (win), FALSE);

  return win;
}
