#include "ooze-shot-window.h"

#include "ooze-application.h"

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  GtkWindow *existing;
  GtkWidget *window;

  existing = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (existing && OOZE_SHOT_IS_WINDOW (existing))
    {
      gtk_window_present (existing);
      return;
    }

  window = ooze_shot_window_new (GTK_APPLICATION (app));
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;

  app = ooze_application_new ("org.ooze.Shot", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
