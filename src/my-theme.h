#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _MyTheme MyTheme;

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
} MyAquaPalette;

typedef void (*MyThemeChangedCallback) (gpointer user_data);

MyTheme *my_theme_new (void);
MyTheme *my_theme_get_default (void);
gboolean my_theme_is_dark (MyTheme *theme);
const MyAquaPalette *my_theme_get_palette (MyTheme *theme);
void my_theme_toggle (MyTheme *theme);
/* Fires after the palette has flipped (repaint shell chrome). */
void my_theme_watch (MyTheme *theme, MyThemeChangedCallback callback, gpointer user_data);
void my_theme_unwatch (MyTheme *theme, MyThemeChangedCallback callback, gpointer user_data);
/* Fires before the palette flips (snapshot the old frame for transitions). */
void my_theme_watch_will_change (MyTheme               *theme,
                                 MyThemeChangedCallback callback,
                                 gpointer               user_data);
void my_theme_unwatch_will_change (MyTheme               *theme,
                                   MyThemeChangedCallback callback,
                                   gpointer               user_data);
void my_theme_free (MyTheme *theme);

G_END_DECLS
