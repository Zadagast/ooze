#include "ooze-ear-window.h"

#include "ooze-shared-icons.h"
#include "ooze-theme.h"

#include <adwaita.h>

static void
on_startup (AdwApplication *app G_GNUC_UNUSED,
            gpointer        user_data G_GNUC_UNUSED)
{
  ooze_icons_configure_gtk ();
  ooze_theme_ensure ();
}

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  GtkWidget *win = ooze_ear_window_new (GTK_APPLICATION (app));
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (win));
  gtk_window_present (GTK_WINDOW (win));
}

int
main (int argc, char **argv)
{
  g_autoptr (AdwApplication) app = NULL;

  ooze_icons_apply ();

  app = adw_application_new ("org.ooze.Ear", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
