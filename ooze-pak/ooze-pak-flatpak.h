#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  char *app_id;
  char *name;
  char *version;
  char *branch;
  char *origin;
  char *installation; /* "user" or "system" */
} OozePakApp;

void ooze_pak_app_free (OozePakApp *app);

gboolean ooze_pak_flatpak_available (void);

/* List installed applications. Caller frees with g_list_free_full(..., ooze_pak_app_free). */
GList *ooze_pak_flatpak_list_apps (GError **error);

/* Install a local .flatpak / .flatpakref / remote ref. Runs async via callback. */
typedef void (*OozePakFlatpakDone) (gboolean  ok,
                                    const char *message,
                                    gpointer   user_data);

void ooze_pak_flatpak_install_async (const char         *path_or_ref,
                                     OozePakFlatpakDone  callback,
                                     gpointer            user_data);

void ooze_pak_flatpak_uninstall_async (const char         *app_id,
                                       const char         *installation,
                                       OozePakFlatpakDone  callback,
                                       gpointer            user_data);

gboolean ooze_pak_path_looks_installable (const char *path);

G_END_DECLS
