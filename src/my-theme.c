#include "my-theme.h"

#include <gio/gio.h>
#include <string.h>

static const MyAquaPalette palette_light = {
  .pinstripe_base_r = 0.86, .pinstripe_base_g = 0.86, .pinstripe_base_b = 0.88,
  .pinstripe_highlight_a = 0.65,
  .pinstripe_shadow_a = 0.18,
  .menu_text_r = 0.08, .menu_text_g = 0.08, .menu_text_b = 0.08,
  .title_text_r = 0.16, .title_text_g = 0.16, .title_text_b = 0.16,
  .wallpaper_center_r = 0.52, .wallpaper_center_g = 0.68, .wallpaper_center_b = 0.92,
  .wallpaper_mid_r = 0.34, .wallpaper_mid_g = 0.52, .wallpaper_mid_b = 0.82,
  .wallpaper_edge_r = 0.16, .wallpaper_edge_g = 0.30, .wallpaper_edge_b = 0.58,
  .dock_top_a = 0.78, .dock_mid_a = 0.58, .dock_bottom_a = 0.45,
  .dock_border_a = 0.55,
};

static const MyAquaPalette palette_dark = {
  .pinstripe_base_r = 0.20, .pinstripe_base_g = 0.20, .pinstripe_base_b = 0.22,
  .pinstripe_highlight_a = 0.12,
  .pinstripe_shadow_a = 0.35,
  .menu_text_r = 0.92, .menu_text_g = 0.92, .menu_text_b = 0.94,
  .title_text_r = 0.90, .title_text_g = 0.90, .title_text_b = 0.92,
  .wallpaper_center_r = 0.18, .wallpaper_center_g = 0.20, .wallpaper_center_b = 0.26,
  .wallpaper_mid_r = 0.12, .wallpaper_mid_g = 0.14, .wallpaper_mid_b = 0.20,
  .wallpaper_edge_r = 0.06, .wallpaper_edge_g = 0.07, .wallpaper_edge_b = 0.11,
  .dock_top_a = 0.42, .dock_mid_a = 0.32, .dock_bottom_a = 0.24,
  .dock_border_a = 0.28,
};

typedef struct _MyThemeWatcher MyThemeWatcher;

struct _MyThemeWatcher
{
  MyThemeChangedCallback cb;
  gpointer data;
};

struct _MyTheme
{
  GSettings *settings;
  gulong color_scheme_handler;
  gulong gtk_theme_handler;
  gboolean dark;
  GSList *watchers;
  GSList *will_change_watchers;
};

static MyTheme *default_theme;

static gboolean
my_theme_gtk_theme_is_dark (GSettings *settings)
{
  g_autofree char *gtk_theme = NULL;

  gtk_theme = g_settings_get_string (settings, "gtk-theme");
  if (!gtk_theme)
    return FALSE;

  return g_str_has_suffix (gtk_theme, "-dark");
}

static gboolean
my_theme_read_dark (GSettings *settings)
{
  g_autofree char *scheme = NULL;

  scheme = g_settings_get_string (settings, "color-scheme");
  if (!scheme || g_strcmp0 (scheme, "default") == 0)
    return my_theme_gtk_theme_is_dark (settings);
  if (g_strcmp0 (scheme, "prefer-dark") == 0)
    return TRUE;
  if (g_strcmp0 (scheme, "prefer-light") == 0)
    return FALSE;

  return my_theme_gtk_theme_is_dark (settings);
}

static void
my_theme_emit_watchers (GSList *watchers)
{
  for (GSList *l = watchers; l != NULL; l = l->next)
    {
      MyThemeWatcher *w = l->data;

      w->cb (w->data);
    }
}

static void
my_theme_apply (MyTheme *theme)
{
  gboolean dark;

  dark = my_theme_read_dark (theme->settings);
  if (dark == theme->dark)
    return;

  /* Snapshot / transition hooks still see the old palette. */
  my_theme_emit_watchers (theme->will_change_watchers);

  theme->dark = dark;

  my_theme_emit_watchers (theme->watchers);
}

static void
my_theme_on_setting_changed (GSettings   *settings G_GNUC_UNUSED,
                             const char  *key G_GNUC_UNUSED,
                             MyTheme     *theme)
{
  my_theme_apply (theme);
}

MyTheme *
my_theme_new (void)
{
  MyTheme *theme;

  theme = g_new0 (MyTheme, 1);
  theme->settings = g_settings_new ("org.gnome.desktop.interface");
  theme->dark = my_theme_read_dark (theme->settings);

  theme->color_scheme_handler =
    g_signal_connect (theme->settings,
                      "changed::color-scheme",
                      G_CALLBACK (my_theme_on_setting_changed),
                      theme);
  theme->gtk_theme_handler =
    g_signal_connect (theme->settings,
                      "changed::gtk-theme",
                      G_CALLBACK (my_theme_on_setting_changed),
                      theme);

  return theme;
}

MyTheme *
my_theme_get_default (void)
{
  if (!default_theme)
    default_theme = my_theme_new ();

  return default_theme;
}

gboolean
my_theme_is_dark (MyTheme *theme)
{
  if (!theme)
    theme = my_theme_get_default ();

  return theme->dark;
}

const MyAquaPalette *
my_theme_get_palette (MyTheme *theme)
{
  if (!theme)
    theme = my_theme_get_default ();

  return theme->dark ? &palette_dark : &palette_light;
}

void
my_theme_toggle (MyTheme *theme)
{
  if (!theme)
    theme = my_theme_get_default ();

  /* Write to GSettings; the "changed::color-scheme" signal handler will
   * call my_theme_apply() which fires all registered watchers so the panel,
   * dock, and wallpaper repaint themselves with the new palette immediately. */
  g_settings_set_string (theme->settings,
                         "color-scheme",
                         theme->dark ? "prefer-light" : "prefer-dark");
}

void
my_theme_watch (MyTheme               *theme,
                MyThemeChangedCallback callback,
                gpointer               user_data)
{
  MyThemeWatcher *w;

  if (!theme)
    theme = my_theme_get_default ();
  if (!callback)
    return;

  w = g_new (MyThemeWatcher, 1);
  w->cb = callback;
  w->data = user_data;
  theme->watchers = g_slist_append (theme->watchers, w);
}

static void
my_theme_remove_watcher (GSList                **list,
                         MyThemeChangedCallback  callback,
                         gpointer                user_data)
{
  GSList *l;

  for (l = *list; l != NULL; l = l->next)
    {
      MyThemeWatcher *w = l->data;

      if (w->cb == callback && w->data == user_data)
        {
          *list = g_slist_delete_link (*list, l);
          g_free (w);
          return;
        }
    }
}

void
my_theme_unwatch (MyTheme               *theme,
                  MyThemeChangedCallback callback,
                  gpointer               user_data)
{
  if (!theme)
    theme = my_theme_get_default ();

  my_theme_remove_watcher (&theme->watchers, callback, user_data);
}

void
my_theme_watch_will_change (MyTheme               *theme,
                            MyThemeChangedCallback callback,
                            gpointer               user_data)
{
  MyThemeWatcher *w;

  if (!theme)
    theme = my_theme_get_default ();
  if (!callback)
    return;

  w = g_new (MyThemeWatcher, 1);
  w->cb = callback;
  w->data = user_data;
  theme->will_change_watchers = g_slist_append (theme->will_change_watchers, w);
}

void
my_theme_unwatch_will_change (MyTheme               *theme,
                              MyThemeChangedCallback callback,
                              gpointer               user_data)
{
  if (!theme)
    theme = my_theme_get_default ();

  my_theme_remove_watcher (&theme->will_change_watchers, callback, user_data);
}

void
my_theme_free (MyTheme *theme)
{
  if (!theme)
    return;

  if (theme == default_theme)
    default_theme = NULL;

  if (theme->color_scheme_handler)
    g_signal_handler_disconnect (theme->settings, theme->color_scheme_handler);
  if (theme->gtk_theme_handler)
    g_signal_handler_disconnect (theme->settings, theme->gtk_theme_handler);

  g_clear_object (&theme->settings);
  g_slist_free_full (theme->watchers, g_free);
  g_slist_free_full (theme->will_change_watchers, g_free);
  g_free (theme);
}
