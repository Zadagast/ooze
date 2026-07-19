#include "spot-window.h"

#include "ooze-application.h"
#include "ooze-shared-appmenu.h"

static const char file_manager_introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.FileManager1'>"
  "    <method name='ShowItems'>"
  "      <arg name='uris' type='as' direction='in'/>"
  "      <arg name='startup_id' type='s' direction='in'/>"
  "    </method>"
  "    <method name='ShowFolders'>"
  "      <arg name='uris' type='as' direction='in'/>"
  "      <arg name='startup_id' type='s' direction='in'/>"
  "    </method>"
  "    <method name='ShowItemProperties'>"
  "      <arg name='uris' type='as' direction='in'/>"
  "      <arg name='startup_id' type='s' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static GDBusNodeInfo *file_manager_introspection;
static guint file_manager_owner_id;
static guint file_manager_registration_id;
static GDBusConnection *file_manager_connection;

static SpotWindow *
spot_pick_window (AdwApplication *app)
{
  GtkWindow *win;
  GList *windows;

  win = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (SPOT_IS_WINDOW (win))
    return SPOT_WINDOW (win);

  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  if (windows && SPOT_IS_WINDOW (windows->data))
    return SPOT_WINDOW (windows->data);
  return NULL;
}

static SpotWindow *
spot_ensure_window (AdwApplication *app)
{
  SpotWindow *window = spot_pick_window (app);

  if (window)
    {
      gtk_window_present (GTK_WINDOW (window));
      return window;
    }

  window = spot_window_new (GTK_APPLICATION (app));
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  gtk_window_present (GTK_WINDOW (window));
  return window;
}

static void
spot_file_manager_method_call (GDBusConnection       *connection G_GNUC_UNUSED,
                                const char            *sender G_GNUC_UNUSED,
                                const char            *object_path G_GNUC_UNUSED,
                                const char            *interface_name G_GNUC_UNUSED,
                                const char            *method_name G_GNUC_UNUSED,
                                GVariant              *parameters,
                                GDBusMethodInvocation *invocation,
                                gpointer               user_data)
{
  AdwApplication *app = ADW_APPLICATION (user_data);
  g_autofree char **uris = NULL;
  const char *startup_id = NULL;
  SpotWindow *window;

  g_variant_get (parameters, "(^as&s)", &uris, &startup_id);
  if (uris && uris[0])
    {
      window = spot_ensure_window (app);
      spot_window_reveal_uri (window, uris[0]);
    }

  g_dbus_method_invocation_return_value (invocation, NULL);
}

static const GDBusInterfaceVTable file_manager_vtable = {
  .method_call = spot_file_manager_method_call,
};

static void
spot_file_manager_bus_acquired (GDBusConnection *connection,
                                const char      *name G_GNUC_UNUSED,
                                gpointer         user_data)
{
  g_autoptr (GError) error = NULL;

  file_manager_registration_id =
    g_dbus_connection_register_object (
      connection, "/org/freedesktop/FileManager1",
      file_manager_introspection->interfaces[0],
      &file_manager_vtable, user_data, NULL, &error);
  if (error || file_manager_registration_id == 0)
    g_warning ("Spot: failed to register FileManager1: %s",
               error ? error->message : "unknown error");
  else
    file_manager_connection = g_object_ref (connection);
}

static void
spot_receive_files_activated (GSimpleAction *action G_GNUC_UNUSED,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  AdwApplication *app = ADW_APPLICATION (user_data);
  SpotWindow *win;
  g_autofree const char **paths = NULL;
  gboolean prefer_move = FALSE;

  if (!parameter)
    return;
  g_variant_get (parameter, "(^asb)", &paths, &prefer_move);

  win = spot_pick_window (app);
  if (win && paths)
    {
      spot_window_receive_paths (win, paths, prefer_move);
      spot_window_shell_drag_leave (win);
    }
}

static void
spot_shell_drag_motion_activated (GSimpleAction *action G_GNUC_UNUSED,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
  AdwApplication *app = ADW_APPLICATION (user_data);
  SpotWindow *win;
  double x = 0, y = 0;

  if (!parameter)
    return;
  g_variant_get (parameter, "(dd)", &x, &y);
  win = spot_pick_window (app);
  if (win)
    {
      gtk_window_present (GTK_WINDOW (win));
      spot_window_shell_drag_motion (win, x, y);
    }
}

static void
spot_shell_drag_leave_activated (GSimpleAction *action G_GNUC_UNUSED,
                                 GVariant      *parameter G_GNUC_UNUSED,
                                 gpointer       user_data)
{
  AdwApplication *app = ADW_APPLICATION (user_data);
  SpotWindow *win = spot_pick_window (app);

  if (win)
    spot_window_shell_drag_leave (win);
}

static void
on_startup (AdwApplication *app,
            gpointer        user_data G_GNUC_UNUSED)
{
  const GActionEntry entries[] = {
    { "receive-files", spot_receive_files_activated, "(asb)", NULL, NULL },
    { "shell-drag-motion", spot_shell_drag_motion_activated, "(dd)", NULL, NULL },
    { "shell-drag-leave", spot_shell_drag_leave_activated, NULL, NULL, NULL },
  };

  g_action_map_add_action_entries (G_ACTION_MAP (app), entries,
                                   G_N_ELEMENTS (entries), app);

  file_manager_introspection =
    g_dbus_node_info_new_for_xml (file_manager_introspection_xml, NULL);
  file_manager_owner_id =
    g_bus_own_name (G_BUS_TYPE_SESSION, "org.freedesktop.FileManager1",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    spot_file_manager_bus_acquired, NULL, NULL,
                    app, NULL);
}

static void
on_shutdown (AdwApplication *app G_GNUC_UNUSED,
             gpointer        user_data G_GNUC_UNUSED)
{
  if (file_manager_connection && file_manager_registration_id)
    g_dbus_connection_unregister_object (file_manager_connection,
                                         file_manager_registration_id);
  g_clear_object (&file_manager_connection);
  file_manager_registration_id = 0;

  if (file_manager_owner_id)
    g_bus_unown_name (file_manager_owner_id);
  file_manager_owner_id = 0;

  g_clear_pointer (&file_manager_introspection, g_dbus_node_info_unref);
}

static void
on_activate (AdwApplication *app,
             gpointer        user_data G_GNUC_UNUSED)
{
  SpotWindow *window;
  const char *start_path;

  /* Always open a new window — dock middle-click and a second `spot`
   * launch should not merely raise the existing one. */
  start_path = g_object_get_data (G_OBJECT (app), "start-path");
  window = spot_window_new_for_path (GTK_APPLICATION (app), start_path);
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

          window = spot_window_new_for_path (GTK_APPLICATION (app), path);
          gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
          gtk_window_present (GTK_WINDOW (window));
        }
      else
        {
          g_autoptr (GError) error = NULL;
          g_autoptr (GAppLaunchContext) ctx = g_app_launch_context_new ();
          g_autoptr (GAppInfo) info = NULL;
          const char *uri = g_file_get_uri (files[i]);

          info = g_file_query_default_handler (files[i], NULL, NULL);
          ooze_appmenu_prepare_launch_context_for_info (ctx, info);
          if (info)
            {
              GList uris = { .data = (gpointer) uri, .next = NULL, .prev = NULL };

              if (!g_app_info_launch_uris (info, &uris, ctx, &error))
                g_warning ("Spot: failed to open %s: %s",
                           g_file_get_path (files[i]),
                           error->message);
            }
          else if (!g_app_info_launch_default_for_uri (uri, ctx, &error))
            g_warning ("Spot: failed to open %s: %s",
                       g_file_get_path (files[i]),
                       error->message);
        }
    }
}

int
main (int argc, char **argv)
{
  g_autoptr (OozeApplication) app = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *start_path = NULL;
  const GOptionEntry options[] = {
    { "path", 'p', 0, G_OPTION_ARG_FILENAME, &start_path,
      "Open this folder", "PATH" },
    { NULL }
  };

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, options, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Spot: %s\n", error->message);
      return EXIT_FAILURE;
    }

  app = ooze_application_new (
    "org.ooze.Spot",
    G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_HANDLES_OPEN);
  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (app, "shutdown", G_CALLBACK (on_shutdown), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);
  g_object_set_data_full (G_OBJECT (app),
                          "start-path",
                          g_steal_pointer (&start_path),
                          g_free);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
