#include "ooze-plugin.h"
#include "ooze-theme.h"

#include "ooze-shared-appmenu.h"
#include "ooze-shared-icons.h"

#include <gio/gio.h>
#include <stdlib.h>

#include <meta/meta-context.h>
#include <meta/meta-plugin.h>
#include <meta/prefs.h>

#define WM_NAME "Ooze"

static void
ooze_apply_wm_settings_early (void)
{
  g_autoptr (GSettings) mutter = NULL;
  g_autoptr (GSettings) mouse = NULL;
  g_autoptr (GSettings) wm = NULL;
  GSettingsSchemaSource *source;
  g_autoptr (GSettingsSchema) mouse_schema = NULL;
  g_autoptr (GSettingsSchema) wm_schema = NULL;

  /*
   * Must run before meta_context_setup() so Mutter's prefs bind to
   * edge-tiling=true. Schema default is false; without this, drag-to-edge
   * snap never arms (see mutter meta-window-drag.c).
   */
  mutter = g_settings_new ("org.gnome.mutter");
  if (mutter)
    {
      g_settings_set_boolean (mutter, "edge-tiling", TRUE);
      g_settings_set_int (mutter, "draggable-border-width", 12);
    }

  /* Wider edge hit-zone: mutter uses drag_threshold * 6 for tile strips. */
  source = g_settings_schema_source_get_default ();
  if (source)
    mouse_schema = g_settings_schema_source_lookup (
        source, "org.gnome.desktop.peripherals.mouse", TRUE);
  if (mouse_schema && g_settings_schema_has_key (mouse_schema, "drag-threshold"))
    {
      mouse = g_settings_new_full (mouse_schema, NULL, NULL);
      g_settings_set_int (mouse, "drag-threshold", 16);
    }

  /* Left-side controls for MetaFrames + GTK CSD that honor GNOME prefs. */
  if (source)
    wm_schema = g_settings_schema_source_lookup (
        source, "org.gnome.desktop.wm.preferences", TRUE);
  if (wm_schema && g_settings_schema_has_key (wm_schema, "button-layout"))
    {
      wm = g_settings_new_full (wm_schema, NULL, NULL);
      g_settings_set_string (wm, "button-layout", "close,minimize,maximize:");
    }

  /* Left-side controls for MetaFrames + GTK CSD that honor GNOME prefs.
   * Do NOT set gtk-theme to WhiteSur globally — that overrides Ooze Gel /
   * OozeKit. Foreign apps get GTK_THEME only on their launch environ.
   * Recover any previous global WhiteSur bleed. */
  ooze_theme_recover_ooze_from_foreign_gtk ();

  g_settings_sync ();
}

static MetaContext *
meta_init (int *argc, char **argv[])
{
  g_autoptr (GError) error = NULL;
  MetaContext *context;

  ooze_apply_wm_settings_early ();

  /*
   * Register our plugin GType with Mutter. In libmutter-18 this is done via
   * meta_context_set_plugin_gtype(); meta_context_setup() then calls
   * meta_plugin_manager_set_plugin_type() internally.
   */
  context = meta_create_context (WM_NAME);
  meta_context_set_plugin_gtype (context, OOZE_TYPE_PLUGIN);
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

  g_print ("Ooze: meta_prefs edge-tiling=%s\n",
           meta_prefs_get_edge_tiling () ? "on" : "off");

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

  /*
   * Nest default: 1920×1080 virtual monitor unless the launcher overrides
   * MUTTER_DEBUG_DUMMY_MODE_SPECS (see run-devkit.sh).
   */
  if (!g_getenv ("MUTTER_DEBUG_DUMMY_MODE_SPECS"))
    g_setenv ("MUTTER_DEBUG_DUMMY_MODE_SPECS",
               "1280x720:1600x900:1920x1080:2560x1440",
               TRUE);

  ooze_icons_apply ();
  ooze_appmenu_setup_environment ();
  ooze_appmenu_ensure_registrar ();
  /* ShellShowsMenubar waits until plugin start — nest Xwayland, not host :0. */

  if (!ooze_appmenu_module_available ())
    g_warning ("Ooze: appmenu-gtk-module missing; run scripts/install-appmenu.sh "
               "for Inkscape / GTK3 global menus");

  context = meta_init (&argc, &argv);
  if (!context)
    return EXIT_FAILURE;

  status = meta_run (context);

  meta_context_destroy (context);
  g_steal_pointer (&context);

  return status;
}
