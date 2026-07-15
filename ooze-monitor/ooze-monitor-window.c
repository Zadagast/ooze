#include "ooze-monitor-window.h"
#include "ooze-display-pane.h"

#include "ooze-about.h"

struct _OozeMonitorWindow
{
  OozeApplicationWindow parent_instance;
};

G_DEFINE_FINAL_TYPE (OozeMonitorWindow, ooze_monitor_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

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
monitor_build_help_menu (void)
{
  GMenu *help;

  help = g_menu_new ();
  g_menu_append (help, "About Ooze Monitor", "win.about");
  return G_MENU_MODEL (help);
}

static void ooze_monitor_window_constructed (GObject *object);

static void
ooze_monitor_window_class_init (OozeMonitorWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_monitor_window_constructed;
}

static void
ooze_monitor_window_init (OozeMonitorWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", monitor_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_monitor_window_constructed (GObject *object)
{
  OozeMonitorWindow *self = OOZE_MONITOR_WINDOW (object);
  GMenuModel *help;
  GtkWidget *pane;

  G_OBJECT_CLASS (ooze_monitor_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 420);
  gtk_window_set_icon_name (GTK_WINDOW (self), "video-display");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-monitor");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Display Settings");

  pane = ooze_display_pane_new ();
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), pane);

  help = monitor_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_monitor_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_TYPE_MONITOR_WINDOW,
                                   "application", app,
                                   NULL));
}
