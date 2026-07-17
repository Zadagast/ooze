#include "ooze-theme.h"

#include "ooze-font.h"
#include "ooze-popover.h"
#include "ooze-scroll.h"

#include <adwaita.h>
#include <gtk/gtk.h>

typedef struct
{
  GObject *target;
  GCallback callback;
  gboolean swapped;
} OozeThemeNotify;

static AdwStyleManager *style_manager;
static gboolean style_manager_ready;
static GtkCssProvider *accent_provider;

static void ooze_theme_dark_changed (AdwStyleManager *manager,
                                     GParamSpec     *pspec,
                                     gpointer        user_data);

static void
ooze_theme_load_css (gboolean dark)
{
  const char *accent_color = dark ? "#72a6f2" : "#2968c8";
  g_autofree char *css = g_strdup_printf (
    "@define-color accent_bg_color #2968c8;"
    "@define-color accent_fg_color #ffffff;"
    "@define-color accent_color %s;",
    accent_color);

  gtk_css_provider_load_from_string (accent_provider, css);
}

static void
ooze_theme_notify_free (OozeThemeNotify *notify)
{
  if (!notify)
    return;
  g_clear_object (&notify->target);
  g_free (notify);
}

static gboolean
ooze_theme_style_manager_init_idle (gpointer user_data G_GNUC_UNUSED)
{
  style_manager = g_object_ref (adw_style_manager_get_default ());
  style_manager_ready = TRUE;
  g_signal_connect (style_manager, "notify::dark",
                    G_CALLBACK (ooze_theme_dark_changed), NULL);
  ooze_theme_load_css (adw_style_manager_get_dark (style_manager));
  return G_SOURCE_REMOVE;
}

static void
ooze_theme_dark_changed (AdwStyleManager *manager,
                         GParamSpec     *pspec G_GNUC_UNUSED,
                         gpointer        user_data G_GNUC_UNUSED)
{
  ooze_theme_load_css (adw_style_manager_get_dark (manager));
}

static gboolean
ooze_theme_connect_dark_notify_idle (gpointer user_data)
{
  OozeThemeNotify *notify = user_data;

  if (style_manager_ready)
    {
      if (notify->target)
        g_signal_connect_object (style_manager,
                                 "notify::dark",
                                 notify->callback,
                                 notify->target,
                                 notify->swapped ? G_CONNECT_SWAPPED : 0);
      else
        g_signal_connect (style_manager, "notify::dark",
                          notify->callback, NULL);

      if (notify->swapped)
        ((void (*) (gpointer)) notify->callback) (notify->target);
      else if (notify->target)
        ((void (*) (gpointer, GParamSpec *, gpointer)) notify->callback)
          (style_manager, NULL, notify->target);
      else
        ((void (*) (gpointer, GParamSpec *)) notify->callback)
          (style_manager, NULL);
    }
  ooze_theme_notify_free (notify);
  return G_SOURCE_REMOVE;
}

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
    "}"
    /*
     * Shared Ooze Gel CSD — 9px corners + Aqua drop shadow.
     * Apps opt in with CSS class "spot-finder" (historical name from Spot;
     * King/Pak/Eye use the same class so every process gets this look via
     * ooze_theme_ensure, not only when Spot’s own CSS is loaded).
     */
    "window.csd.spot-finder > decoration {"
    "  border-radius: 9px;"
    "  box-shadow:"
    "    0 2px  6px rgba(0,0,0,0.22),"
    "    0 8px 24px rgba(0,0,0,0.40),"
    "    0 20px 40px rgba(0,0,0,0.20);"
    "}"
    "window.csd.spot-finder > decoration:focus {"
    "  box-shadow:"
    "    0 2px  6px rgba(0,0,0,0.28),"
    "    0 10px 30px rgba(0,0,0,0.48),"
    "    0 22px 44px rgba(0,0,0,0.22);"
    "}"
    "window.csd.spot-finder,"
    ".spot-finder {"
    "  background: @window_bg_color;"
    "}");
  accent_provider = gtk_css_provider_new ();
  ooze_theme_load_css (FALSE);
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (accent_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
  g_object_unref (p);

  /* Aqua sliding-window scrollbars (always-visible proportional thumbs). */
  ooze_scroll_ensure_css ();

  /* Let GtkPopoverMenu grow to monitor height; scroll only when needed. */
  map_signal = g_signal_lookup ("map", GTK_TYPE_POPOVER_MENU);
  if (map_signal != 0)
    g_signal_add_emission_hook (map_signal, 0,
                                ooze_theme_popover_map_hook, NULL, NULL);

  g_idle_add_full (G_PRIORITY_LOW,
                   ooze_theme_style_manager_init_idle,
                   NULL,
                   NULL);
  loaded = TRUE;
}

gboolean
ooze_theme_is_dark (void)
{
  return style_manager_ready && adw_style_manager_get_dark (style_manager);
}

void
ooze_theme_connect_dark_notify (GObject    *target,
                                GCallback   callback)
{
  ooze_theme_connect_dark_notify_full (target, callback, TRUE);
}

void
ooze_theme_connect_dark_notify_full (GObject    *target,
                                     GCallback   callback,
                                     gboolean    swapped)
{
  OozeThemeNotify *notify;

  g_return_if_fail (target == NULL || G_IS_OBJECT (target));
  g_return_if_fail (callback != NULL);

  notify = g_new0 (OozeThemeNotify, 1);
  notify->target = target ? g_object_ref (target) : NULL;
  notify->callback = callback;
  notify->swapped = swapped;
  g_idle_add_full (G_PRIORITY_LOW,
                   ooze_theme_connect_dark_notify_idle,
                   notify,
                   NULL);
}
