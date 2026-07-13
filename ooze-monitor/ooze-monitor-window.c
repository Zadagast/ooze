#include "ooze-monitor-window.h"
#include "ooze-display-pane.h"

#include "ooze-about.h"
#include "ooze-header-bar.h"
#include "ooze-window-actions.h"

struct _OozeMonitorWindow
{
  GtkApplicationWindow parent_instance;
  GtkWidget *header;
};

G_DEFINE_FINAL_TYPE (OozeMonitorWindow, ooze_monitor_window, GTK_TYPE_APPLICATION_WINDOW)

static void
monitor_action_about (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant      *param G_GNUC_UNUSED,
                        gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Monitor",
                      "video-display",
                      "Display settings for Ooze Desktop.",
                      OOZE_VERSION);
}

static GMenuModel *
monitor_build_menubar (void)
{
  GMenu *bar, *help;
  GMenuItem *item;

  bar = g_menu_new ();
  ooze_menubar_append_edit (bar);
  ooze_menubar_append_window (bar);
  help = g_menu_new ();
  g_menu_append (help, "About Ooze Monitor", "win.about");
  item = g_menu_item_new_submenu ("Help", G_MENU_MODEL (help));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (help);
  return G_MENU_MODEL (bar);
}

static void
ooze_monitor_window_class_init (OozeMonitorWindowClass *klass G_GNUC_UNUSED)
{
}

static void
ooze_monitor_window_init (OozeMonitorWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", monitor_action_about, NULL, NULL, NULL },
  };
  GtkWidget *pane;

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 420);
  gtk_window_set_title (GTK_WINDOW (self), "Display Settings");
  gtk_window_set_icon_name (GTK_WINDOW (self), "video-display");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-monitor");

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
  ooze_window_actions_add_chrome (GTK_APPLICATION_WINDOW (self));
  ooze_window_actions_add_edit (GTK_APPLICATION_WINDOW (self));

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "Display Settings");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  pane = ooze_display_pane_new ();
  gtk_window_set_child (GTK_WINDOW (self), pane);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
ooze_monitor_window_new (GtkApplication *app)
{
  OozeMonitorWindow *win;
  GMenuModel *menubar;

  win = g_object_new (OOZE_TYPE_MONITOR_WINDOW, "application", app, NULL);

  menubar = monitor_build_menubar ();
  gtk_application_set_menubar (app, menubar);
  g_object_unref (menubar);
  gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (win), FALSE);

  return GTK_WIDGET (win);
}
