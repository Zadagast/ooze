#include "my-plugin.h"

#include <stdlib.h>

#include <meta/meta-context.h>
#include <meta/meta-plugin.h>

#define WM_NAME "My Desktop"

static MetaContext *
meta_init (int *argc, char **argv[])
{
  g_autoptr (GError) error = NULL;
  MetaContext *context;

  /*
   * Register our plugin GType with Mutter. In libmutter-18 this is done via
   * meta_context_set_plugin_gtype(); meta_context_setup() then calls
   * meta_plugin_manager_set_plugin_type() internally.
   */
  context = meta_create_context (WM_NAME);
  meta_context_set_plugin_gtype (context, MY_TYPE_PLUGIN);
  meta_context_set_gnome_wm_keybindings (context, "super");

  if (!meta_context_configure (context, argc, argv, &error))
    {
      g_printerr ("Failed to configure Mutter context: %s\n", error->message);
      g_object_unref (context);
      return NULL;
    }

  if (!meta_context_setup (context, &error))
    {
      g_printerr ("Failed to setup Mutter context: %s\n", error->message);
      g_object_unref (context);
      return NULL;
    }

  if (!meta_context_start (context, &error))
    {
      g_printerr ("Failed to start Mutter context: %s\n", error->message);
      g_object_unref (context);
      return NULL;
    }

  return context;
}

static int
meta_run (MetaContext *context)
{
  g_autoptr (GError) error = NULL;

  if (!meta_context_run_main_loop (context, &error))
    {
      g_printerr ("Mutter main loop exited with error: %s\n", error->message);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  int status;

  context = meta_init (&argc, &argv);
  if (!context)
    return EXIT_FAILURE;

  status = meta_run (context);

  meta_context_destroy (context);
  g_steal_pointer (&context);

  return status;
}
