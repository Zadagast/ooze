#include "ooze-ear-window.h"
#include "ooze-sound-pane.h"

#include "ooze-about.h"

struct _OozeEarWindow
{
  OozeApplicationWindow parent_instance;
};

G_DEFINE_FINAL_TYPE (OozeEarWindow, ooze_ear_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void
ear_action_about (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *param G_GNUC_UNUSED,
                  gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Ear",
                      "audio-headphones",
                      "Sound Settings for Ooze Desktop.",
                      OOZE_VERSION);
}

static GMenuModel *
ear_build_help_menu (void)
{
  GMenu *help;

  help = g_menu_new ();
  g_menu_append (help, "About Ooze Ear", "win.about");
  return G_MENU_MODEL (help);
}

static void ooze_ear_window_constructed (GObject *object);

static void
ooze_ear_window_class_init (OozeEarWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_ear_window_constructed;
}

static void
ooze_ear_window_init (OozeEarWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", ear_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_ear_window_constructed (GObject *object)
{
  OozeEarWindow *self = OOZE_EAR_WINDOW (object);
  GMenuModel *help;
  GtkWidget *pane;

  G_OBJECT_CLASS (ooze_ear_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 480);
  gtk_window_set_icon_name (GTK_WINDOW (self), "audio-headphones");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-ear");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Sound Settings");

  pane = ooze_sound_pane_new ();
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), pane);

  help = ear_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_ear_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_EAR_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
