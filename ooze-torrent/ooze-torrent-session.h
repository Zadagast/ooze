#pragma once

#include <glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

typedef struct OozeTrSession OozeTrSession;
typedef void (*OozeTrSessionFreeDoneFunc) (gpointer user_data);

typedef enum {
  OOZE_TR_STATE_STOPPED = 0,
  OOZE_TR_STATE_CHECK_WAIT,
  OOZE_TR_STATE_CHECK,
  OOZE_TR_STATE_DOWNLOAD_WAIT,
  OOZE_TR_STATE_DOWNLOAD,
  OOZE_TR_STATE_SEED_WAIT,
  OOZE_TR_STATE_SEED,
} OozeTrState;

typedef struct {
  int          id;
  char        *name;
  double       progress; /* 0..1 */
  double       down_KBps;
  double       up_KBps;
  OozeTrState  state;
  char        *download_dir;
} OozeTrTorrentInfo;

OozeTrSession *ooze_tr_session_new  (GError **error);
void           ooze_tr_session_free (OozeTrSession *self);
void           ooze_tr_session_free_async (OozeTrSession             *self,
                                           OozeTrSessionFreeDoneFunc   done,
                                           gpointer                    user_data);

/* Add from a .torrent path or magnet URI. Returns torrent id, or -1. */
int  ooze_tr_session_add_file   (OozeTrSession *self,
                                 const char    *path,
                                 GError       **error);
int  ooze_tr_session_add_magnet (OozeTrSession *self,
                                 const char    *magnet,
                                 GError       **error);

void ooze_tr_session_start  (OozeTrSession *self, int id);
void ooze_tr_session_stop   (OozeTrSession *self, int id);
void ooze_tr_session_remove (OozeTrSession *self, int id, bool delete_data);

/* Caller frees the array with ooze_tr_session_free_list(). */
OozeTrTorrentInfo *ooze_tr_session_list (OozeTrSession *self,
                                         guint         *n_out);
void               ooze_tr_session_free_list (OozeTrTorrentInfo *list,
                                              guint              n);

const char *ooze_tr_state_label (OozeTrState state);

G_END_DECLS
