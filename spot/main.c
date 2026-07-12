#include "spot-window.h"

#include "my-icons.h"

#include <adwaita.h>

static void
on_startup (AdwApplication *app G_GNUC_UNUSED,
            gpointer        user_data G_GNUC_UNUSED)
{
  my_icons_configure_gtk ();
}

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  GtkWindow *existing;
  SpotWindow *window;

  existing = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (existing)
    {
      gtk_window_present (existing);
      return;
    }

  window = spot_window_new (app);
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (window));
}

static void
on_open (AdwApplication *app,
         GFile          **files,
         int             n_files,
         const char      *hint G_GNUC_UNUSED)
{
  SpotWindow *window;
  int i;

  for (i = 0; i < n_files; i++)
    {
      g_autoptr (GFileInfo) info = NULL;
      GFileType type;

      info = g_file_query_info (files[i],
                                G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                G_FILE_QUERY_INFO_NONE,
                                NULL,
                                NULL);
      if (!info)
        continue;

      type = g_file_info_get_file_type (info);
      if (type == G_FILE_TYPE_DIRECTORY)
        {
          g_autofree char *path = g_file_get_path (files[i]);

          window = spot_window_new_for_path (app, path);
          gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
          gtk_window_present (GTK_WINDOW (window));
        }
      else
        {
          g_autoptr (GError) error = NULL;

          if (!g_app_info_launch_default_for_uri (g_file_get_uri (files[i]),
                                                  NULL,
                                                  &error))
            g_warning ("Spot: failed to open %s: %s",
                       g_file_get_path (files[i]),
                       error->message);
        }
    }
}

int
main (int argc, char **argv)
{
  g_autoptr (AdwApplication) app = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *start_path = NULL;
  const GOptionEntry options[] = {
    { "path", 'p', 0, G_OPTION_ARG_FILENAME, &start_path,
      "Open this folder", "PATH" },
    { NULL }
  };

  my_icons_apply ();

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Spot: %s\n", error->message);
      return EXIT_FAILURE;
    }

  app = adw_application_new ("org.ooze.Spot",
                             G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_HANDLES_OPEN);
  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);
  g_object_set_data_full (G_OBJECT (app),
                          "start-path",
                          g_steal_pointer (&start_path),
                          g_free);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
