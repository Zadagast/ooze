#pragma once

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef enum {
  OOZE_EAR_NODE_SINK = 0,
  OOZE_EAR_NODE_SOURCE,
  OOZE_EAR_NODE_PLAYBACK,
  OOZE_EAR_NODE_RECORD,
} OozeEarNodeKind;

typedef struct {
  uint32_t        id;
  OozeEarNodeKind kind;
  char           *name;
  char           *node_name;     /* PipeWire node.name */
  char           *object_serial; /* object.serial — used for stream routing */
  char           *detail;        /* app name / media name, may be NULL */
  char           *target;        /* current target.object for streams, may be NULL */
  float           volume;        /* linear 0..1 (channel average) */
  gboolean        mute;
  gboolean        is_default;
  guint           channels;
} OozeEarNodeInfo;

typedef struct _OozeEarPw OozeEarPw;

/* Called on the GLib main thread whenever the node list changes. */
typedef void (*OozeEarPwChangedFunc) (gpointer user_data);

OozeEarPw *ooze_ear_pw_new     (OozeEarPwChangedFunc changed,
                                gpointer             user_data);
void       ooze_ear_pw_free    (OozeEarPw           *self);

/* Snapshot of current nodes; free with ooze_ear_pw_free_nodes(). */
GPtrArray *ooze_ear_pw_snapshot (OozeEarPw *self);
void       ooze_ear_pw_free_nodes (GPtrArray *nodes);

void ooze_ear_pw_set_volume  (OozeEarPw *self, uint32_t id, float volume);
void ooze_ear_pw_set_mute    (OozeEarPw *self, uint32_t id, gboolean mute);
void ooze_ear_pw_set_default (OozeEarPw *self, uint32_t id);

/* Route a playback/record stream to a sink/source node id.
 * Pass target_id == 0 to clear (follow system default). */
void ooze_ear_pw_set_target  (OozeEarPw *self, uint32_t stream_id, uint32_t target_id);

G_END_DECLS
