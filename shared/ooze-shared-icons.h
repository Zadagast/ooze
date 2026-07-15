#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define OOZE_ICON_THEME   "elementary"
#define OOZE_CURSOR_THEME "elementary"
#define OOZE_CURSOR_SIZE  24

void ooze_icons_setup_environment (void);

void ooze_icons_apply (void);

void ooze_icons_apply_to_launcher (GSubprocessLauncher *launcher);

gboolean ooze_icons_theme_is_available (void);

gboolean ooze_icons_theme_name_usable (const char *theme_name);

char *ooze_icons_get_data_root (void);

char *ooze_icons_get_icons_dir (void);

char *ooze_icons_get_elementary_dir (void);

void ooze_icons_configure_gtk (void);

void ooze_icons_configure_gtk_async (void);

gboolean ooze_icons_gtk_has_icon (const char *icon_name);

G_END_DECLS
