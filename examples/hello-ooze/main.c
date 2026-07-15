#include <oozekit.h>

static void
on_activate (GApplication *application)
{
  GtkWidget *window = GTK_WIDGET (ooze_application_window_new (GTK_APPLICATION (application)));

  gtk_window_set_default_size (GTK_WINDOW (window), 640, 360);
  gtk_window_set_title (GTK_WINDOW (window), "Hello, Ooze");
  ooze_application_window_set_content (OOZE_APPLICATION_WINDOW (window),
                                       gtk_label_new ("Hello from OozeKit."));
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = ooze_application_new ("org.ooze.HelloOoze",
                                                           G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
