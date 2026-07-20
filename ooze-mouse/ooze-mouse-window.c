#include "ooze-mouse-window.h"
#include "ooze-mouse-pane.h"

#include "ooze-about.h"

struct _OozeMouseWindow
{
  OozeApplicationWindow parent_instance;
};

G_DEFINE_FINAL_TYPE (OozeMouseWindow, ooze_mouse_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void
mouse_action_about (GSimpleAction *action G_GNUC_UNUSED,
                    GVariant      *param G_GNUC_UNUSED,
                    gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data), "Ooze Mouse",
                      "input-mouse",
                      "Pointer speed, acceleration, and scrolling.",
                      OOZE_VERSION);
}

static GMenuModel *
mouse_build_help_menu (void)
{
  GMenu *help = g_menu_new ();

  g_menu_append (help, "About Ooze Mouse", "win.about");
  return G_MENU_MODEL (help);
}

static void ooze_mouse_window_constructed (GObject *object);

static void
ooze_mouse_window_class_init (OozeMouseWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_mouse_window_constructed;
}

static void
ooze_mouse_window_init (OozeMouseWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", mouse_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self), entries,
                                   G_N_ELEMENTS (entries), self);
}

static void
ooze_mouse_window_constructed (GObject *object)
{
  OozeMouseWindow *self = OOZE_MOUSE_WINDOW (object);
  GMenuModel *help;
  GtkWidget *pane;

  G_OBJECT_CLASS (ooze_mouse_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 520, 380);
  gtk_window_set_icon_name (GTK_WINDOW (self), "input-mouse");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-mouse");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Mouse");

  pane = ooze_mouse_pane_new ();
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), pane);

  help = mouse_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_mouse_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_MOUSE_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
