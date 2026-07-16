/* Ooze Torrent — C++ bridge to libtransmission (GPL-2.0-or-later). */

#include "ooze-torrent-session.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include <glib.h>

#include <cstring>
#include <string>
#include <vector>

struct OozeTrSession
{
  tr_session *session = nullptr;
};

struct SessionFreeAsyncContext
{
  OozeTrSession            *session;
  OozeTrSessionFreeDoneFunc done;
  gpointer                  user_data;
};

static gboolean
session_free_async_complete (gpointer user_data)
{
  auto *context = static_cast<SessionFreeAsyncContext *> (user_data);

  if (context->done)
    context->done (context->user_data);
  delete context;
  return G_SOURCE_REMOVE;
}

static gpointer
session_free_async_worker (gpointer user_data)
{
  auto *context = static_cast<SessionFreeAsyncContext *> (user_data);

  ooze_tr_session_free (context->session);
  g_idle_add (session_free_async_complete, context);
  return nullptr;
}

static OozeTrState
map_activity (tr_torrent_activity a)
{
  switch (a)
    {
    case TR_STATUS_CHECK_WAIT:    return OOZE_TR_STATE_CHECK_WAIT;
    case TR_STATUS_CHECK:         return OOZE_TR_STATE_CHECK;
    case TR_STATUS_DOWNLOAD_WAIT: return OOZE_TR_STATE_DOWNLOAD_WAIT;
    case TR_STATUS_DOWNLOAD:      return OOZE_TR_STATE_DOWNLOAD;
    case TR_STATUS_SEED_WAIT:     return OOZE_TR_STATE_SEED_WAIT;
    case TR_STATUS_SEED:          return OOZE_TR_STATE_SEED;
    case TR_STATUS_STOPPED:
    default:                      return OOZE_TR_STATE_STOPPED;
    }
}

extern "C" const char *
ooze_tr_state_label (OozeTrState state)
{
  switch (state)
    {
    case OOZE_TR_STATE_CHECK_WAIT:    return "Queued to check";
    case OOZE_TR_STATE_CHECK:         return "Checking";
    case OOZE_TR_STATE_DOWNLOAD_WAIT: return "Queued";
    case OOZE_TR_STATE_DOWNLOAD:      return "Downloading";
    case OOZE_TR_STATE_SEED_WAIT:     return "Queued to seed";
    case OOZE_TR_STATE_SEED:          return "Seeding";
    case OOZE_TR_STATE_STOPPED:
    default:                          return "Paused";
    }
}

extern "C" OozeTrSession *
ooze_tr_session_new (GError **error)
{
  g_autofree char *config = g_build_filename (g_get_user_config_dir (),
                                              "ooze-torrent", nullptr);
  g_mkdir_with_parents (config, 0700);

  auto *self = new OozeTrSession ();
  try
    {
      /* Registers tr_variant serializers used by Settings::save/load.
       * Without this, libtransmission prints a flood of
       * "ERROR: No serializer/deserializer registered for type ..." and
       * session defaults are incomplete. */
      tr_lib_init ();
      auto settings = tr_sessionGetDefaultSettings ();
      self->session = tr_sessionInit (config, true, settings);
      if (!self->session)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                       "tr_sessionInit failed");
          delete self;
          return nullptr;
        }

      tr_ctor *ctor = tr_ctorNew (self->session);
      tr_sessionLoadTorrents (self->session, ctor);
      tr_ctorFree (ctor);
    }
  catch (...)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "libtransmission threw while starting");
      delete self;
      return nullptr;
    }

  return self;
}

extern "C" void
ooze_tr_session_free (OozeTrSession *self)
{
  if (!self)
    return;
  if (self->session)
    tr_sessionClose (self->session);
  delete self;
}

extern "C" void
ooze_tr_session_free_async (OozeTrSession             *self,
                            OozeTrSessionFreeDoneFunc  done,
                            gpointer                   user_data)
{
  if (!self)
    {
      if (done)
        g_idle_add (session_free_async_complete,
                    new SessionFreeAsyncContext { nullptr, done, user_data });
      return;
    }

  auto *context = new SessionFreeAsyncContext { self, done, user_data };
  GThread *thread = g_thread_new ("ooze-torrent-close",
                                  session_free_async_worker,
                                  context);
  g_thread_unref (thread);
}

static int
add_with_ctor (OozeTrSession *self,
               bool (*set_meta) (tr_ctor *, char const *, tr_error *),
               char const *arg,
               GError **error)
{
  if (!self || !self->session || !arg)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "invalid arguments");
      return -1;
    }

  tr_ctor *ctor = tr_ctorNew (self->session);
  tr_error err {};
  if (!set_meta (ctor, arg, &err))
    {
      std::string msg = err.message ().empty () ? "failed to parse torrent"
                                                : std::string (err.message ());
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s", msg.c_str ());
      tr_ctorFree (ctor);
      return -1;
    }

  tr_torrent *dup = nullptr;
  tr_torrent *tor = tr_torrentNew (ctor, &dup);
  tr_ctorFree (ctor);

  if (!tor)
    {
      if (dup)
        {
          g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
                       "torrent already in session");
          return tr_torrentId (dup);
        }
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "could not add torrent");
      return -1;
    }

  tr_torrentStart (tor);
  return tr_torrentId (tor);
}

extern "C" int
ooze_tr_session_add_file (OozeTrSession *self, const char *path, GError **error)
{
  return add_with_ctor (self, tr_ctorSetMetainfoFromFile, path, error);
}

extern "C" int
ooze_tr_session_add_magnet (OozeTrSession *self, const char *magnet, GError **error)
{
  return add_with_ctor (self, tr_ctorSetMetainfoFromMagnetLink, magnet, error);
}

extern "C" void
ooze_tr_session_start (OozeTrSession *self, int id)
{
  if (!self || !self->session)
    return;
  tr_torrent *tor = tr_torrentFindFromId (self->session, id);
  if (tor)
    tr_torrentStart (tor);
}

extern "C" void
ooze_tr_session_stop (OozeTrSession *self, int id)
{
  if (!self || !self->session)
    return;
  tr_torrent *tor = tr_torrentFindFromId (self->session, id);
  if (tor)
    tr_torrentStop (tor);
}

extern "C" void
ooze_tr_session_remove (OozeTrSession *self, int id, bool delete_data)
{
  if (!self || !self->session)
    return;
  tr_torrent *tor = tr_torrentFindFromId (self->session, id);
  if (tor)
    tr_torrentRemove (tor, delete_data, nullptr, nullptr);
}

extern "C" void
ooze_tr_session_free_list (OozeTrTorrentInfo *list, guint n)
{
  if (!list)
    return;
  for (guint i = 0; i < n; i++)
    {
      g_free (list[i].name);
      g_free (list[i].download_dir);
    }
  g_free (list);
}

extern "C" OozeTrTorrentInfo *
ooze_tr_session_list (OozeTrSession *self, guint *n_out)
{
  if (n_out)
    *n_out = 0;
  if (!self || !self->session)
    return nullptr;

  size_t const n = tr_sessionGetAllTorrents (self->session, nullptr, 0);
  if (n == 0)
    return nullptr;

  std::vector<tr_torrent *> buf (n);
  tr_sessionGetAllTorrents (self->session, buf.data (), n);

  auto *list = g_new0 (OozeTrTorrentInfo, n);
  for (size_t i = 0; i < n; i++)
    {
      tr_torrent *tor = buf[i];
      tr_stat const *st = tr_torrentStat (tor);
      list[i].id = st->id;
      list[i].name = g_strdup (tr_torrentName (tor));
      list[i].progress = st->percentDone;
      list[i].down_KBps = st->pieceDownloadSpeed_KBps;
      list[i].up_KBps = st->pieceUploadSpeed_KBps;
      list[i].state = map_activity (st->activity);
      char const *dir = tr_torrentGetDownloadDir (tor);
      list[i].download_dir = g_strdup (dir ? dir : "");
    }

  if (n_out)
    *n_out = static_cast<guint> (n);
  return list;
}
