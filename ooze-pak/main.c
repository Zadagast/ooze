#include "ooze-pak-window.h"

#include "ooze-application.h"

#include <stdlib.h>

static char *opt_uninstall = NULL;

static OozePakWindow *
ensure_window (GtkApplication *app)
{
  GtkWindow *existing;
  OozePakWindow *window;

  existing = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (OOZE_PAK_IS_WINDOW (existing))
    return OOZE_PAK_WINDOW (existing);

  window = ooze_pak_window_new (app);
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  return window;
}

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  OozePakWindow *window = ensure_window (GTK_APPLICATION (app));

  if (opt_uninstall && *opt_uninstall)
    {
      ooze_pak_window_uninstall_app (window, opt_uninstall);
      g_clear_pointer (&opt_uninstall, g_free);
    }

  gtk_window_present (GTK_WINDOW (window));
}

static void
on_open (AdwApplication *app,
         GFile          **files,
         int              n_files,
         const char      *hint G_GNUC_UNUSED)
{
  OozePakWindow *window = ensure_window (GTK_APPLICATION (app));
  ooze_pak_window_install_paths (window, files, n_files);
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  const GOptionEntry options[] = {
    { "uninstall", 'u', 0, G_OPTION_ARG_STRING, &opt_uninstall,
      "Confirm uninstall of APP_ID", "APP_ID" },
    { NULL }
  };

  context = g_option_context_new ("[FILE…]");
  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Ooze Pak: %s\n", error->message);
      return EXIT_FAILURE;
    }

  app = ooze_application_new (
    "org.ooze.Pak",
    G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_HANDLES_OPEN);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
