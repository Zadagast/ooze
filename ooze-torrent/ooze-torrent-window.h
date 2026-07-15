#pragma once

#include "ooze-application-window.h"

G_BEGIN_DECLS

#define OOZE_TORRENT_TYPE_WINDOW (ooze_torrent_window_get_type ())
G_DECLARE_FINAL_TYPE (OozeTorrentWindow, ooze_torrent_window, OOZE_TORRENT,
                      WINDOW, OozeApplicationWindow)

GtkWidget *ooze_torrent_window_new (GtkApplication *app);

void ooze_torrent_window_open_file (OozeTorrentWindow *self, GFile *file);
void ooze_torrent_window_open_uri  (OozeTorrentWindow *self, const char *uri);

G_END_DECLS
