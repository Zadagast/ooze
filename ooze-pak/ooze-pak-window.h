#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_PAK_TYPE_WINDOW (ooze_pak_window_get_type ())
G_DECLARE_FINAL_TYPE (OozePakWindow, ooze_pak_window, OOZE_PAK, WINDOW,
                      OozeApplicationWindow)

OozePakWindow *ooze_pak_window_new (GtkApplication *app);

void ooze_pak_window_install_paths (OozePakWindow *self,
                                    GFile        **files,
                                    int            n_files);

void ooze_pak_window_uninstall_app (OozePakWindow *self,
                                    const char    *app_id);

G_END_DECLS
