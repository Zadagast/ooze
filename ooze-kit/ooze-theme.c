#include "ooze-theme.h"

#include "ooze-font.h"

#include <gtk/gtk.h>

void
ooze_theme_ensure (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay *display;
  GtkSettings *settings;
  GtkCssProvider *p;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  settings = gtk_settings_get_for_display (display);
  if (settings)
    g_object_set (settings, "gtk-font-name", OOZE_UI_FONT, NULL);

  p = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (p,
    "window, label, button, entry, textview, popover, menu, menuitem,"
    " listview, gridview, columnview, checkbutton, spinbutton,"
    " combobox, dropdown, calendar, searchbar {"
    "  font-family: \"" OOZE_UI_FONT_FAMILY "\", sans-serif;"
    "  font-size: 11pt;"
    "  font-weight: 400;"
    "}"
    /* Kit chrome captions / titles — regular unless .ooze-emphasis */
    ".ooze-button-label,"
    ".ooze-header-title,"
    ".ooze-settings-tile .ooze-button-label,"
    ".ooze-settings-tile .ooze-settings-tile-sub {"
    "  font-family: \"" OOZE_UI_FONT_FAMILY "\", sans-serif;"
    "  font-size: 11pt;"
    "  font-weight: 400;"
    "}"
    ".ooze-emphasis {"
    "  font-weight: 600;"
    "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
  g_object_unref (p);

  loaded = TRUE;
}
