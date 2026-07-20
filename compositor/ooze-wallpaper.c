#include "ooze-wallpaper.h"

#include "ooze-aqua-draw.h"
#include "ooze-screensaver.h"
#include "ooze-theme.h"
#include "ooze-plugin-priv.h"
#include "ooze-wallpaper-source.h"

#include <gio/gio.h>

ClutterContent *
ooze_wallpaper_content (OozePlugin   *plugin,
                        ClutterActor *ref_actor,
                        int           width,
                        int           height)
{
  g_autofree char *uri = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autofree char *picture_uri = NULL;
  g_autofree char *picture_uri_dark = NULL;
  g_autofree char *picture_options = NULL;
  gboolean dark;

  g_return_val_if_fail (OOZE_IS_PLUGIN (plugin), NULL);

  if (plugin->background_settings)
    {
      picture_uri =
        g_settings_get_string (plugin->background_settings, "picture-uri");
      picture_uri_dark =
        g_settings_get_string (plugin->background_settings,
                               "picture-uri-dark");
      picture_options =
        g_settings_get_string (plugin->background_settings,
                               "picture-options");
    }

  dark = ooze_theme_is_dark (NULL);
  uri = ooze_wallpaper_select_uri (picture_uri, picture_uri_dark, dark);
  if (uri && g_strcmp0 (picture_options, "none") != 0)
    {
      pixbuf = ooze_wallpaper_load_pixbuf (uri, width, height);
      if (pixbuf)
        return ooze_aqua_content_from_pixbuf (ref_actor, pixbuf);
    }

  return ooze_aqua_wallpaper_content (ref_actor, width, height);
}

void
ooze_wallpaper_refresh (OozePlugin *plugin)
{
  ClutterActor *child;

  if (plugin->background_group)
    for (child = clutter_actor_get_first_child (plugin->background_group);
         child != NULL;
         child = clutter_actor_get_next_sibling (child))
      {
        g_autoptr (ClutterContent) content = NULL;
        int width = (int) clutter_actor_get_width (child);
        int height = (int) clutter_actor_get_height (child);

        if (width < 1 || height < 1)
          continue;

        content = ooze_wallpaper_content (plugin, child, width, height);
        if (content)
          ooze_aqua_actor_set_content (child,
                                       g_steal_pointer (&content),
                                       width,
                                       height);
      }

  ooze_screensaver_refresh_wallpaper (plugin);
}

static void
ooze_wallpaper_on_settings_changed (GSettings  *settings G_GNUC_UNUSED,
                                    const char *key,
                                    gpointer    user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    {
      ooze_wallpaper_refresh (plugin);
      if (settings == plugin->scenery_settings &&
          g_strcmp0 (key, "screensaver-mode") == 0)
        ooze_screensaver_mode_changed (plugin);
    }
}

void
ooze_wallpaper_init (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  plugin->background_settings = g_settings_new ("org.gnome.desktop.background");
  plugin->scenery_settings = g_settings_new ("org.ooze.scenery");

  g_signal_connect (plugin->background_settings, "changed::picture-uri",
                    G_CALLBACK (ooze_wallpaper_on_settings_changed), plugin);
  g_signal_connect (plugin->background_settings, "changed::picture-uri-dark",
                    G_CALLBACK (ooze_wallpaper_on_settings_changed), plugin);
  g_signal_connect (plugin->background_settings, "changed::picture-options",
                    G_CALLBACK (ooze_wallpaper_on_settings_changed), plugin);
  g_signal_connect (plugin->scenery_settings, "changed::screensaver-mode",
                    G_CALLBACK (ooze_wallpaper_on_settings_changed), plugin);
}

void
ooze_wallpaper_dispose (OozePlugin *plugin)
{
  if (!plugin)
    return;

  if (plugin->background_settings)
    g_signal_handlers_disconnect_by_data (plugin->background_settings, plugin);
  if (plugin->scenery_settings)
    g_signal_handlers_disconnect_by_data (plugin->scenery_settings, plugin);
  g_clear_object (&plugin->background_settings);
  g_clear_object (&plugin->scenery_settings);
}
