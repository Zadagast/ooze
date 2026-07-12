#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define OOZE_ICON_THEME   "elementary"
#define OOZE_CURSOR_THEME "elementary"
#define OOZE_CURSOR_SIZE  24

void my_icons_setup_environment (void);

void my_icons_apply (void);

void my_icons_apply_to_launcher (GSubprocessLauncher *launcher);

gboolean my_icons_theme_is_available (void);

char *my_icons_get_data_root (void);

char *my_icons_get_icons_dir (void);

char *my_icons_get_elementary_dir (void);

void my_icons_configure_gtk (void);

gboolean my_icons_gtk_has_icon (const char *icon_name);

G_END_DECLS
