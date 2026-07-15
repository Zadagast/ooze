#include "ooze-defaults-window.h"
#include "ooze-defaults-pane.h"

#include "ooze-about.h"

struct _OozeDefaultsWindow
{
  OozeApplicationWindow parent_instance;
};

G_DEFINE_FINAL_TYPE (OozeDefaultsWindow, ooze_defaults_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void
defaults_action_about (GSimpleAction *action G_GNUC_UNUSED,
                       GVariant      *param G_GNUC_UNUSED,
                       gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data), "Ooze Default Applications",
                      "preferences-desktop-default-applications",
                      "Choose the default applications for common tasks.",
                      OOZE_VERSION);
}

static GMenuModel *
defaults_build_help_menu (void)
{
  GMenu *help = g_menu_new ();

  g_menu_append (help, "About Ooze Default Applications", "win.about");
  return G_MENU_MODEL (help);
}

static void ooze_defaults_window_constructed (GObject *object);

static void
ooze_defaults_window_class_init (OozeDefaultsWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_defaults_window_constructed;
}

static void
ooze_defaults_window_init (OozeDefaultsWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", defaults_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self), entries,
                                   G_N_ELEMENTS (entries), self);
}

static void
ooze_defaults_window_constructed (GObject *object)
{
  OozeDefaultsWindow *self = OOZE_DEFAULTS_WINDOW (object);
  GMenuModel *help;
  GtkWidget *pane;

  G_OBJECT_CLASS (ooze_defaults_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 620, 560);
  gtk_window_set_icon_name (
    GTK_WINDOW (self), "preferences-desktop-default-applications");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-defaults");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Default Applications");

  pane = ooze_defaults_pane_new ();
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), pane);

  help = defaults_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_defaults_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_DEFAULTS_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
