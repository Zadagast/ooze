#include "ooze-theme.h"

#include "ooze-font.h"
#include "ooze-popover.h"
#include "ooze-scroll.h"

#include <gtk/gtk.h>

static gboolean
ooze_theme_popover_map_hook (GSignalInvocationHint *ihint G_GNUC_UNUSED,
                             guint                  n_param_values,
                             const GValue          *param_values,
                             gpointer               data G_GNUC_UNUSED)
{
  GObject *obj;

  if (n_param_values < 1)
    return TRUE;

  obj = g_value_get_object (&param_values[0]);
  if (GTK_IS_POPOVER_MENU (obj))
    ooze_popover_fit_screen (GTK_POPOVER (obj));

  return TRUE;
}

void
ooze_theme_ensure (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay *display;
  GtkSettings *settings;
  GtkCssProvider *p;
  guint map_signal;

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

  /* Aqua sliding-window scrollbars (always-visible proportional thumbs). */
  ooze_scroll_ensure_css ();

  /* Let GtkPopoverMenu grow to monitor height; scroll only when needed. */
  map_signal = g_signal_lookup ("map", GTK_TYPE_POPOVER_MENU);
  if (map_signal != 0)
    g_signal_add_emission_hook (map_signal, 0,
                                ooze_theme_popover_map_hook, NULL, NULL);

  loaded = TRUE;
}
