#include "ooze-theme.h"
#include "ooze-color-scheme.h"
#include "ooze-foreign-gtk.h"
#include "ooze-xsettings.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

static const OozeAquaPalette palette_light = {
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

static const OozeAquaPalette palette_dark = {
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

typedef struct _OozeThemeWatcher OozeThemeWatcher;

struct _OozeThemeWatcher
{
  OozeThemeChangedCallback cb;
  gpointer data;
};

struct _OozeTheme
{
  GSettings *settings;
  gulong color_scheme_handler;
  gulong icon_theme_handler;
  GFileMonitor *foreign_gtk_monitor;
  gboolean dark;
  GSList *watchers;
  GSList *will_change_watchers;
};

static OozeTheme *default_theme;

const char *
ooze_theme_foreign_gtk_name (gboolean dark)
{
  return dark ? OOZE_GTK_THEME_DARK : OOZE_GTK_THEME_LIGHT;
}

gboolean
ooze_theme_foreign_gtk_installed (gboolean dark)
{
  return ooze_foreign_gtk_variant_installed (dark);
}

static gboolean
ooze_theme_gtk4_config_is_ooze_managed (const char *config_path)
{
  g_autofree char *resolved = NULL;
  g_autofree char *prefix = NULL;

  if (!g_file_test (config_path, G_FILE_TEST_IS_SYMLINK))
    return FALSE;

  resolved = g_file_read_link (config_path, NULL);
  if (!resolved)
    return FALSE;

  if (!g_path_is_absolute (resolved))
    {
      g_autofree char *dir = g_path_get_dirname (config_path);
      g_autofree char *abs = g_build_filename (dir, resolved, NULL);
      g_free (resolved);
      resolved = g_steal_pointer (&abs);
    }

  prefix = g_build_filename (g_get_user_data_dir (), "ooze", "whitesur", NULL);
  return g_str_has_prefix (resolved, prefix);
}

/*
 * Global WhiteSur gtk-theme + ~/.config/gtk-4.0 override bleeds into every
 * GTK4/libadwaita process — including Ooze apps. Undo that session damage.
 * Foreign X11 apps receive WhiteSur through XSETTINGS.
 */
void
ooze_theme_recover_ooze_from_foreign_gtk (void)
{
  g_autofree char *config = NULL;
  g_autoptr (GSettings) iface = NULL;
  g_autofree char *current = NULL;

  config = g_build_filename (g_get_home_dir (), ".config", "gtk-4.0", NULL);
  if (ooze_theme_gtk4_config_is_ooze_managed (config))
    {
      if (g_remove (config) == 0)
        g_print ("Ooze theme: removed WhiteSur gtk-4.0 symlink (protects Ooze apps)\n");
      else
        g_warning ("Ooze theme: failed to remove managed gtk-4.0 symlink at %s",
                   config);
    }

  iface = g_settings_new ("org.gnome.desktop.interface");
  current = g_settings_get_string (iface, "gtk-theme");
  if (current && g_str_has_prefix (current, "WhiteSur"))
    {
      g_settings_set_string (iface, "gtk-theme", "Adwaita");
      g_print ("Ooze theme: reset gtk-theme from %s to Adwaita\n", current);
    }

  ooze_xsettings_republish ();
}

char *
ooze_theme_foreign_gtk_theme_for_session (void)
{
  return ooze_foreign_gtk_theme_for_session ();
}

/* Legacy entry used by older call sites — recovers session bleed only. */
void
ooze_theme_apply_foreign_gtk (gboolean dark G_GNUC_UNUSED)
{
  ooze_theme_recover_ooze_from_foreign_gtk ();
}

static void
ooze_theme_emit_watchers (GSList *watchers)
{
  for (GSList *l = watchers; l != NULL; l = l->next)
    {
      OozeThemeWatcher *w = l->data;

      w->cb (w->data);
    }
}

static guint xsettings_republish_idle_id;

static gboolean
ooze_theme_xsettings_republish_idle (gpointer data G_GNUC_UNUSED)
{
  xsettings_republish_idle_id = 0;
  ooze_xsettings_republish ();
  return G_SOURCE_REMOVE;
}

static void
ooze_theme_schedule_xsettings_republish (void)
{
  /* Defer off the GSettings notify stack / Clutter click handler so XSync
   * does not run re-entrantly against nest Xwayland on the same turn. */
  if (xsettings_republish_idle_id != 0)
    return;

  xsettings_republish_idle_id =
    g_idle_add (ooze_theme_xsettings_republish_idle, NULL);
}

static void
ooze_theme_apply (OozeTheme *theme)
{
  gboolean dark;

  dark = ooze_color_scheme_is_dark (theme->settings);
  if (dark == theme->dark)
    return;

  ooze_theme_emit_watchers (theme->will_change_watchers);
  theme->dark = dark;
  ooze_theme_emit_watchers (theme->watchers);
}

static void
ooze_theme_on_color_scheme_changed (GSettings *settings G_GNUC_UNUSED,
                                    const char *key G_GNUC_UNUSED,
                                    OozeTheme *theme)
{
  /*
   * Appearance toggle only. Do not call recover_ooze_from_foreign_gtk here —
   * that resets gtk-theme and can churn icon/theme paths on the same turn.
   */
  ooze_theme_apply (theme);
  ooze_theme_schedule_xsettings_republish ();
}

static void
ooze_theme_on_icon_theme_changed (GSettings *settings G_GNUC_UNUSED,
                                  const char *key G_GNUC_UNUSED,
                                  OozeTheme *theme G_GNUC_UNUSED)
{
  ooze_theme_schedule_xsettings_republish ();
}

static void
ooze_theme_on_foreign_gtk_changed (GFileMonitor      *monitor G_GNUC_UNUSED,
                                   GFile             *file G_GNUC_UNUSED,
                                   GFile             *other_file G_GNUC_UNUSED,
                                   GFileMonitorEvent  event_type G_GNUC_UNUSED,
                                   OozeTheme         *theme G_GNUC_UNUSED)
{
  /* Pref file only affects foreign launch / XSETTINGS — not Ooze appearance. */
  ooze_theme_schedule_xsettings_republish ();
}

OozeTheme *
ooze_theme_new (void)
{
  OozeTheme *theme;

  theme = g_new0 (OozeTheme, 1);
  theme->settings = g_settings_new ("org.gnome.desktop.interface");
  theme->dark = ooze_color_scheme_is_dark (theme->settings);

  theme->color_scheme_handler =
    g_signal_connect (theme->settings,
                      "changed::color-scheme",
                      G_CALLBACK (ooze_theme_on_color_scheme_changed),
                      theme);
  theme->icon_theme_handler =
    g_signal_connect (theme->settings,
                      "changed::icon-theme",
                      G_CALLBACK (ooze_theme_on_icon_theme_changed),
                      theme);
  {
    g_autofree char *pref_path = ooze_foreign_gtk_pref_path ();
    g_autoptr (GFile) pref_file = g_file_new_for_path (pref_path);

    theme->foreign_gtk_monitor =
      g_file_monitor_file (pref_file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (theme->foreign_gtk_monitor)
      g_signal_connect (theme->foreign_gtk_monitor, "changed",
                        G_CALLBACK (ooze_theme_on_foreign_gtk_changed), theme);
  }

  /* One-shot recovery from the earlier global WhiteSur experiment. */
  ooze_theme_recover_ooze_from_foreign_gtk ();

  return theme;
}

OozeTheme *
ooze_theme_get_default (void)
{
  if (!default_theme)
    default_theme = ooze_theme_new ();

  return default_theme;
}

gboolean
ooze_theme_is_dark (OozeTheme *theme)
{
  if (!theme)
    theme = ooze_theme_get_default ();

  return theme->dark;
}

const OozeAquaPalette *
ooze_theme_get_palette (OozeTheme *theme)
{
  if (!theme)
    theme = ooze_theme_get_default ();

  return theme->dark ? &palette_dark : &palette_light;
}

void
ooze_theme_toggle (OozeTheme *theme)
{
  if (!theme)
    theme = ooze_theme_get_default ();

  g_settings_set_string (theme->settings,
                         "color-scheme",
                         theme->dark ? "prefer-light" : "prefer-dark");
}

void
ooze_theme_watch (OozeTheme               *theme,
                OozeThemeChangedCallback callback,
                gpointer               user_data)
{
  OozeThemeWatcher *w;

  if (!theme)
    theme = ooze_theme_get_default ();
  if (!callback)
    return;

  w = g_new (OozeThemeWatcher, 1);
  w->cb = callback;
  w->data = user_data;
  theme->watchers = g_slist_append (theme->watchers, w);
}

static void
ooze_theme_remove_watcher (GSList                **list,
                         OozeThemeChangedCallback  callback,
                         gpointer                user_data)
{
  GSList *l;

  for (l = *list; l != NULL; l = l->next)
    {
      OozeThemeWatcher *w = l->data;

      if (w->cb == callback && w->data == user_data)
        {
          *list = g_slist_delete_link (*list, l);
          g_free (w);
          return;
        }
    }
}

void
ooze_theme_unwatch (OozeTheme               *theme,
                  OozeThemeChangedCallback callback,
                  gpointer               user_data)
{
  if (!theme)
    theme = ooze_theme_get_default ();

  ooze_theme_remove_watcher (&theme->watchers, callback, user_data);
}

void
ooze_theme_watch_will_change (OozeTheme               *theme,
                            OozeThemeChangedCallback callback,
                            gpointer               user_data)
{
  OozeThemeWatcher *w;

  if (!theme)
    theme = ooze_theme_get_default ();
  if (!callback)
    return;

  w = g_new (OozeThemeWatcher, 1);
  w->cb = callback;
  w->data = user_data;
  theme->will_change_watchers = g_slist_append (theme->will_change_watchers, w);
}

void
ooze_theme_unwatch_will_change (OozeTheme               *theme,
                              OozeThemeChangedCallback callback,
                              gpointer               user_data)
{
  if (!theme)
    theme = ooze_theme_get_default ();

  ooze_theme_remove_watcher (&theme->will_change_watchers, callback, user_data);
}

void
ooze_theme_free (OozeTheme *theme)
{
  if (!theme)
    return;

  if (theme == default_theme)
    default_theme = NULL;

  if (xsettings_republish_idle_id != 0)
    {
      g_source_remove (xsettings_republish_idle_id);
      xsettings_republish_idle_id = 0;
    }

  if (theme->color_scheme_handler)
    g_signal_handler_disconnect (theme->settings, theme->color_scheme_handler);
  if (theme->icon_theme_handler)
    g_signal_handler_disconnect (theme->settings, theme->icon_theme_handler);
  g_clear_object (&theme->foreign_gtk_monitor);
  g_clear_object (&theme->settings);
  g_slist_free_full (theme->watchers, g_free);
  g_slist_free_full (theme->will_change_watchers, g_free);
  g_free (theme);
}
