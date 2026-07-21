#pragma once

#include <gtk/gtk.h>

/*
 * OozeWatchPlayer — GtkGLArea that renders video with libmpv.
 *
 * mpv owns decoding, audio output, and A/V sync; this widget owns the GL
 * surface it renders into.  UI state (time, duration, pause, volume, title)
 * is mirrored from mpv property events and announced with ::changed.
 */
#define OOZE_WATCH_TYPE_PLAYER (ooze_watch_player_get_type ())
G_DECLARE_FINAL_TYPE (OozeWatchPlayer, ooze_watch_player, OOZE_WATCH,
                      PLAYER, GtkGLArea)

GtkWidget *ooze_watch_player_new (void);

void     ooze_watch_player_open         (OozeWatchPlayer *self, GFile *file);
void     ooze_watch_player_toggle_pause (OozeWatchPlayer *self);
void     ooze_watch_player_seek_frac    (OozeWatchPlayer *self, double frac);
void     ooze_watch_player_seek_rel     (OozeWatchPlayer *self, double secs);
void     ooze_watch_player_frame_step   (OozeWatchPlayer *self, gboolean back);
void     ooze_watch_player_set_volume   (OozeWatchPlayer *self, double vol01);

double      ooze_watch_player_get_volume   (OozeWatchPlayer *self);
gboolean    ooze_watch_player_get_paused   (OozeWatchPlayer *self);
double      ooze_watch_player_get_time     (OozeWatchPlayer *self);
double      ooze_watch_player_get_duration (OozeWatchPlayer *self);
const char *ooze_watch_player_get_title    (OozeWatchPlayer *self);
gboolean    ooze_watch_player_has_media    (OozeWatchPlayer *self);
