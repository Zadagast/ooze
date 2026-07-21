#include "ooze-watch-window.h"

#include "ooze-application.h"

static OozeWatchWindow *
ensure_window (GtkApplication *app)
{
  GtkWindow *existing;
  GtkWidget *win;

  existing = gtk_application_get_active_window (app);
  if (existing && OOZE_WATCH_IS_WINDOW (existing))
    return OOZE_WATCH_WINDOW (existing);

  win = ooze_watch_window_new (app);
  gtk_application_add_window (app, GTK_WINDOW (win));
  return OOZE_WATCH_WINDOW (win);
}

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  OozeWatchWindow *win = ensure_window (GTK_APPLICATION (app));

  gtk_window_present (GTK_WINDOW (win));
}

static void
on_open (GApplication  *app,
         GFile        **files,
         gint           n_files,
         const char    *hint G_GNUC_UNUSED,
         gpointer       user_data G_GNUC_UNUSED)
{
  OozeWatchWindow *win;

  if (n_files < 1)
    return;

  win = ensure_window (GTK_APPLICATION (app));
  ooze_watch_window_open_file (win, files[0]);
  gtk_window_present (GTK_WINDOW (win));
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;

  app = ooze_application_new ("org.ooze.Watch", G_APPLICATION_HANDLES_OPEN);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
