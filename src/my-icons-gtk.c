#include "my-icons.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>

/*
 * In GTK4, gtk_icon_theme_get_for_display() returns the display-singleton
 * theme.  The singleton's theme-name must be changed via GtkSettings
 * ("gtk-icon-theme-name"), NOT via gtk_icon_theme_set_theme_name() which
 * asserts !is_display_singleton and crashes.  We only add the extra search
 * path directly on the theme object, which IS allowed on the singleton.
 */
static void
my_icons_ensure_search_path (GtkIconTheme *theme)
{
  g_autofree char *icons_dir = my_icons_get_icons_dir ();

  if (icons_dir && g_file_test (icons_dir, G_FILE_TEST_IS_DIR))
    gtk_icon_theme_add_search_path (theme, icons_dir);
}

static void
on_icon_theme_changed (GtkIconTheme *theme,
                       gpointer      user_data G_GNUC_UNUSED)
{
  /*
   * After a theme change (e.g. Adwaita resetting the theme name) re-apply
   * via GtkSettings so the theme name is correct, then re-add our path.
   */
  GdkDisplay *display = gdk_display_get_default ();
  if (display)
    {
      GtkSettings *settings = gtk_settings_get_for_display (display);
      if (settings)
        {
          g_signal_handlers_block_by_func (theme, on_icon_theme_changed, user_data);
          g_object_set (settings, "gtk-icon-theme-name", OOZE_ICON_THEME, NULL);
          my_icons_ensure_search_path (theme);
          g_signal_handlers_unblock_by_func (theme, on_icon_theme_changed, user_data);
        }
    }
}

void
my_icons_configure_gtk (void)
{
  GdkDisplay *display;
  GtkIconTheme *theme;
  GtkSettings *settings;

  my_icons_apply ();

  display = gdk_display_get_default ();
  if (!display)
    return;

  /* Theme name must be set via GtkSettings – direct API on singleton crashes. */
  settings = gtk_settings_get_for_display (display);
  if (settings)
    g_object_set (settings, "gtk-icon-theme-name", OOZE_ICON_THEME, NULL);

  /* Add extra search path (allowed on singleton). */
  theme = gtk_icon_theme_get_for_display (display);
  my_icons_ensure_search_path (theme);

  if (!g_signal_handler_find (theme, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                              on_icon_theme_changed, NULL))
    g_signal_connect (theme, "changed", G_CALLBACK (on_icon_theme_changed), NULL);
}

gboolean
my_icons_gtk_has_icon (const char *icon_name)
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
