#include "ooze-defaults-window.h"

#include "ooze-application.h"

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  GtkWidget *win = ooze_defaults_window_new (GTK_APPLICATION (app));

  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (win));
  gtk_window_present (GTK_WINDOW (win));
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;

  app = ooze_application_new ("org.ooze.Defaults", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
