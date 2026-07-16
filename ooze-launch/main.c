#include "ooze-launch-window.h"

#include "ooze-application.h"

static void
on_activate (GApplication *application,
             gpointer      user_data G_GNUC_UNUSED)
{
  GtkWindow *window;

  window = gtk_application_get_active_window (GTK_APPLICATION (application));
  if (!window)
    {
      window = GTK_WINDOW (ooze_launch_window_new (
        GTK_APPLICATION (application)));
      gtk_application_add_window (GTK_APPLICATION (application), window);
    }

  gtk_window_present (window);
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;

  app = ooze_application_new ("org.ooze.Launch", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
