#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define OOZE_GTK_THEME_LIGHT "WhiteSur-Light"
#define OOZE_GTK_THEME_DARK  "WhiteSur-Dark"

typedef struct _OozeTheme OozeTheme;

typedef struct
{
  gdouble pinstripe_base_r;
  gdouble pinstripe_base_g;
  gdouble pinstripe_base_b;
  gdouble pinstripe_highlight_a;
  gdouble pinstripe_shadow_a;
  gdouble menu_text_r;
  gdouble menu_text_g;
  gdouble menu_text_b;
  gdouble title_text_r;
  gdouble title_text_g;
  gdouble title_text_b;
  gdouble wallpaper_center_r;
  gdouble wallpaper_center_g;
  gdouble wallpaper_center_b;
  gdouble wallpaper_mid_r;
  gdouble wallpaper_mid_g;
  gdouble wallpaper_mid_b;
  gdouble wallpaper_edge_r;
  gdouble wallpaper_edge_g;
  gdouble wallpaper_edge_b;
  gdouble dock_top_a;
  gdouble dock_mid_a;
  gdouble dock_bottom_a;
  gdouble dock_border_a;
} OozeAquaPalette;

typedef void (*OozeThemeChangedCallback) (gpointer user_data);

OozeTheme *ooze_theme_new (void);
OozeTheme *ooze_theme_get_default (void);
gboolean ooze_theme_is_dark (OozeTheme *theme);
const OozeAquaPalette *ooze_theme_get_palette (OozeTheme *theme);
void ooze_theme_toggle (OozeTheme *theme);

/* WhiteSur helpers — never applied globally via GSettings (that breaks Ooze Gel).
 * X11 foreign apps get WhiteSur through XSETTINGS + launch-scoped GTK_THEME. */
const char *ooze_theme_foreign_gtk_name (gboolean dark);
gboolean    ooze_theme_foreign_gtk_installed (gboolean dark);
/* Returns an allocated selected foreign GTK theme, or NULL if missing. */
char       *ooze_theme_foreign_gtk_theme_for_session (void);
void        ooze_theme_recover_ooze_from_foreign_gtk (void);
void        ooze_theme_apply_foreign_gtk_to_launcher (GSubprocessLauncher *launcher);
void        ooze_theme_apply_foreign_gtk_to_launch_context (GAppLaunchContext *ctx);
/* Legacy entry used by main.c — recovers session bleed, does not apply WhiteSur. */
void        ooze_theme_apply_foreign_gtk (gboolean dark);

void ooze_theme_watch (OozeTheme *theme, OozeThemeChangedCallback callback, gpointer user_data);
void ooze_theme_unwatch (OozeTheme *theme, OozeThemeChangedCallback callback, gpointer user_data);
void ooze_theme_watch_will_change (OozeTheme               *theme,
                                 OozeThemeChangedCallback callback,
                                 gpointer               user_data);
void ooze_theme_unwatch_will_change (OozeTheme               *theme,
                                   OozeThemeChangedCallback callback,
                                   gpointer               user_data);
void ooze_theme_free (OozeTheme *theme);

G_END_DECLS
