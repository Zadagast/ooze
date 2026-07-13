#include "ooze-shared-icons.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>

/*
 * GTK4-specific icon theme integration.
 *
 * In GTK4, gtk_icon_theme_get_for_display() returns the display-singleton
 * theme.  The singleton's theme-name must be changed via GtkSettings
 * ("gtk-icon-theme-name"), NOT via gtk_icon_theme_set_theme_name() which
 * asserts !is_display_singleton and crashes.  We only add the extra search
 * path directly on the theme object, which IS allowed on the singleton.
 */
static char *
ooze_icons_gtk_preferred_theme_name (void)
{
  g_autoptr (GSettings) settings = g_settings_new ("org.gnome.desktop.interface");
  g_autofree char *name = g_settings_get_string (settings, "icon-theme");

  if (name && name[0] != '\0')
    return g_steal_pointer (&name);

  return g_strdup (OOZE_ICON_THEME);
}

static void
ooze_icons_ensure_search_path (GtkIconTheme *theme)
{
  g_autofree char *icons_dir = ooze_icons_get_icons_dir ();

  if (icons_dir && g_file_test (icons_dir, G_FILE_TEST_IS_DIR))
    gtk_icon_theme_add_search_path (theme, icons_dir);
}

static void
on_icon_theme_changed (GtkIconTheme *theme,
                       gpointer      user_data G_GNUC_UNUSED)
{
  /*
   * After a theme change re-sync GtkSettings from GSettings, then re-add
   * our search path. Do not force elementary over the user's pack.
   * Only write gtk-icon-theme-name when it differs — GtkSettings syncs to
   * GSettings and a no-op set still churns changed::icon-theme, which made
   * the compositor rebuild the dock on every Light↔Dark restyle.
   */
  GdkDisplay *display = gdk_display_get_default ();
  if (display)
    {
      GtkSettings *settings = gtk_settings_get_for_display (display);
      if (settings)
        {
          g_autofree char *name = ooze_icons_gtk_preferred_theme_name ();
          g_autofree char *current = NULL;

          g_object_get (settings, "gtk-icon-theme-name", &current, NULL);
          if (g_strcmp0 (current, name) != 0)
            {
              g_signal_handlers_block_by_func (theme, on_icon_theme_changed,
                                               user_data);
              g_object_set (settings, "gtk-icon-theme-name", name, NULL);
              g_signal_handlers_unblock_by_func (theme, on_icon_theme_changed,
                                                 user_data);
            }
          ooze_icons_ensure_search_path (theme);
        }
    }
}

void
ooze_icons_configure_gtk (void)
{
  GdkDisplay *display;
  GtkIconTheme *theme;
  GtkSettings *settings;
  g_autofree char *name = NULL;

  ooze_icons_apply ();

  display = gdk_display_get_default ();
  if (!display)
    return;

  name = ooze_icons_gtk_preferred_theme_name ();

  /* Theme name must be set via GtkSettings – direct API on singleton crashes. */
  settings = gtk_settings_get_for_display (display);
  if (settings)
    g_object_set (settings, "gtk-icon-theme-name", name, NULL);

  /* Add extra search path (allowed on singleton). */
  theme = gtk_icon_theme_get_for_display (display);
  ooze_icons_ensure_search_path (theme);

  if (!g_signal_handler_find (theme, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                              on_icon_theme_changed, NULL))
    g_signal_connect (theme, "changed", G_CALLBACK (on_icon_theme_changed), NULL);
}

gboolean
ooze_icons_gtk_has_icon (const char *icon_name)
{
  GdkDisplay *display;
  GtkIconTheme *theme;

  if (!icon_name || icon_name[0] == '\0')
    return FALSE;

  display = gdk_display_get_default ();
  if (!display)
    return FALSE;

  theme = gtk_icon_theme_get_for_display (display);
  return gtk_icon_theme_has_icon (theme, icon_name);
}
