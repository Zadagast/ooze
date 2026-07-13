#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define OOZE_FOREIGN_GTK_LIGHT "WhiteSur-Light"
#define OOZE_FOREIGN_GTK_DARK  "WhiteSur-Dark"
/* Pref value meaning "follow light/dark appearance". */
#define OOZE_FOREIGN_GTK_AUTO  "auto"

gboolean    ooze_foreign_gtk_theme_installed (const char *theme_name);
gboolean    ooze_foreign_gtk_variant_installed (gboolean dark);

/* Prefer ~/.config/ooze/foreign-gtk-theme; "auto"/empty → Light/Dark by appearance. */
char       *ooze_foreign_gtk_theme_for_session (void);
const char *ooze_foreign_gtk_default_for_dark (gboolean dark);

/* Read/write ~/.config/ooze/foreign-gtk-theme (theme name or "auto"). */
char       *ooze_foreign_gtk_pref_get (void);
gboolean    ooze_foreign_gtk_pref_set (const char *theme_or_auto);

char       *ooze_foreign_gtk_pref_path (void);

/* Installed GTK themes under XDG themes dirs (name has index.theme). */
char      **ooze_foreign_gtk_list_themes (void);

void        ooze_foreign_gtk_apply_to_launcher (GSubprocessLauncher *launcher);
void        ooze_foreign_gtk_apply_to_launch_context (GAppLaunchContext *ctx);

G_END_DECLS
