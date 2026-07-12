#define _GNU_SOURCE

#include "ooze-ear-pw.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/dict.h>
#include <spa/utils/keys.h>
#include <spa/utils/json.h>

#include <string.h>

typedef struct {
  OozeEarPw          *pw;
  uint32_t            id;
  OozeEarNodeKind     kind;
  struct pw_proxy    *proxy;
  struct spa_hook     proxy_listener;
  struct spa_hook     object_listener;
  char               *name;
  char               *node_name;
  char               *object_serial;
  char               *detail;
  char               *target; /* metadata target.object */
  float               volume;
  gboolean            mute;
  guint               channels;
} EarNode;

struct _OozeEarPw
{
  struct pw_thread_loop *loop;
  struct pw_context     *context;
  struct pw_core        *core;
  struct pw_registry    *registry;
  struct pw_proxy       *metadata;
  struct spa_hook        registry_listener;
  struct spa_hook        core_listener;
  struct spa_hook        metadata_listener;

  GHashTable            *nodes; /* id -> EarNode* */
  GMutex                 lock;

  char                  *default_sink;
  char                  *default_source;
  guint                  suppress_notify;

  OozeEarPwChangedFunc   changed;
  gpointer               user_data;
  guint                  idle_id;
  gboolean               pending_notify;
};

/* ── Notify UI on the GLib main thread ───────────────────────────────────── */

static gboolean
notify_idle (gpointer user_data)
{
  OozeEarPw *self = user_data;

  self->idle_id = 0;
  if (!self->pending_notify)
    return G_SOURCE_REMOVE;

  self->pending_notify = FALSE;
  if (self->changed)
    self->changed (self->user_data);
  return G_SOURCE_REMOVE;
}

static void
request_notify (OozeEarPw *self)
{
  if (self->suppress_notify > 0)
    return;

  self->pending_notify = TRUE;
  if (self->idle_id == 0)
    self->idle_id = g_idle_add (notify_idle, self);
}

static char *
json_get_name (const char *json)
{
  const char *p;
  const char *end;
  gsize len;

  if (!json)
    return NULL;

  p = strstr (json, "\"name\"");
  if (!p)
    return NULL;
  p = strchr (p + 6, '"');
  if (!p)
    return NULL;
  p++;
  end = strchr (p, '"');
  if (!end || end <= p)
    return NULL;
  len = (gsize) (end - p);
  return g_strndup (p, len);
}

/* ── Node helpers ────────────────────────────────────────────────────────── */

static void
ear_node_free (gpointer p)
{
  EarNode *n = p;

  if (!n)
    return;

  if (n->proxy)
    {
      pw_proxy_destroy (n->proxy);
      n->proxy = NULL;
    }

  g_free (n->name);
  g_free (n->node_name);
  g_free (n->object_serial);
  g_free (n->detail);
  g_free (n->target);
  g_free (n);
}

static OozeEarNodeKind
kind_from_media_class (const char *mc)
{
  if (!mc)
    return (OozeEarNodeKind) -1;
  if (g_strcmp0 (mc, "Audio/Sink") == 0)
    return OOZE_EAR_NODE_SINK;
  if (g_strcmp0 (mc, "Audio/Source") == 0)
    return OOZE_EAR_NODE_SOURCE;
  if (g_strcmp0 (mc, "Stream/Output/Audio") == 0)
    return OOZE_EAR_NODE_PLAYBACK;
  if (g_strcmp0 (mc, "Stream/Input/Audio") == 0)
    return OOZE_EAR_NODE_RECORD;
  return (OozeEarNodeKind) -1;
}

static float
average_volumes (const float *vols, uint32_t n)
{
  float sum = 0.f;
  uint32_t i;

  if (!vols || n == 0)
    return 0.f;
  for (i = 0; i < n; i++)
    sum += vols[i];
  return sum / (float) n;
}

static void
parse_props_pod (EarNode *node, const struct spa_pod *param)
{
  struct spa_pod_prop *prop;
  struct spa_pod_object *obj;

  if (!param || !spa_pod_is_object_type (param, SPA_TYPE_OBJECT_Props))
    return;

  obj = (struct spa_pod_object *) param;
  SPA_POD_OBJECT_FOREACH (obj, prop)
    {
      switch (prop->key)
        {
        case SPA_PROP_mute:
          {
            bool mute = false;
            if (spa_pod_get_bool (&prop->value, &mute) == 0)
              node->mute = mute ? TRUE : FALSE;
          }
          break;

        case SPA_PROP_channelVolumes:
        case SPA_PROP_softVolumes:
          {
            uint32_t n_vals = 0;
            float *vals = spa_pod_get_array (&prop->value, &n_vals);

            if (!vals || n_vals == 0)
              break;
            node->channels = n_vals;
            node->volume = average_volumes (vals, n_vals);
            if (node->volume < 0.f)
              node->volume = 0.f;
            if (node->volume > 1.f)
              node->volume = 1.f;
          }
          break;

        default:
          break;
        }
    }
}

/* ── Node events ─────────────────────────────────────────────────────────── */

static void
on_node_info (void                      *data,
              const struct pw_node_info *info)
{
  EarNode *node = data;
  const char *mc;
  const char *desc;
  const char *app;
  const char *media;
  OozeEarNodeKind kind;
  uint32_t i;

  if (!info)
    return;

  mc = spa_dict_lookup (info->props, PW_KEY_MEDIA_CLASS);
  kind = kind_from_media_class (mc);
  if ((int) kind >= 0)
    node->kind = kind;

  desc = spa_dict_lookup (info->props, PW_KEY_NODE_DESCRIPTION);
  if (!desc)
    desc = spa_dict_lookup (info->props, PW_KEY_NODE_NICK);
  if (!desc)
    desc = spa_dict_lookup (info->props, PW_KEY_NODE_NAME);
  if (!desc)
    desc = "Audio";

  g_free (node->name);
  node->name = g_strdup (desc);

  {
    const char *nn = spa_dict_lookup (info->props, PW_KEY_NODE_NAME);
    if (nn && g_strcmp0 (nn, node->node_name) != 0)
      {
        g_free (node->node_name);
        node->node_name = g_strdup (nn);
      }
  }

  {
    const char *serial = spa_dict_lookup (info->props, PW_KEY_OBJECT_SERIAL);
    if (!serial)
      serial = spa_dict_lookup (info->props, "object.serial");
    if (serial && g_strcmp0 (serial, node->object_serial) != 0)
      {
        g_free (node->object_serial);
        node->object_serial = g_strdup (serial);
      }
  }

  {
    const char *tgt = spa_dict_lookup (info->props, PW_KEY_TARGET_OBJECT);
    if (!tgt)
      tgt = spa_dict_lookup (info->props, "node.target");
    /* Metadata target wins when present; only fill from props if empty. */
    if (tgt && !node->target)
      node->target = g_strdup (tgt);
  }

  app = spa_dict_lookup (info->props, PW_KEY_APP_NAME);
  media = spa_dict_lookup (info->props, PW_KEY_MEDIA_NAME);
  g_free (node->detail);
  if (app && media)
    node->detail = g_strdup_printf ("%s — %s", app, media);
  else if (app)
    node->detail = g_strdup (app);
  else if (media)
    node->detail = g_strdup (media);
  else
    node->detail = NULL;

  if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS)
    {
      for (i = 0; i < info->n_params; i++)
        {
          if (info->params[i].id == SPA_PARAM_Props &&
              (info->params[i].flags & SPA_PARAM_INFO_READ))
            {
              pw_node_enum_params ((struct pw_node *) node->proxy,
                                   0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);
              break;
            }
        }
    }

  request_notify (node->pw);
}

static void
on_node_param (void                 *data,
               int                   seq G_GNUC_UNUSED,
               uint32_t              id,
               uint32_t              index G_GNUC_UNUSED,
               uint32_t              next G_GNUC_UNUSED,
               const struct spa_pod *param)
{
  EarNode *node = data;

  if (id == SPA_PARAM_Props)
    {
      parse_props_pod (node, param);
      request_notify (node->pw);
    }
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = on_node_info,
  .param = on_node_param,
};

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
};

static void bind_metadata_if_needed (OozeEarPw             *self,
                                     uint32_t               id,
                                     const char            *type,
                                     const struct spa_dict *props);

/* ── Registry ────────────────────────────────────────────────────────────── */

static void
registry_global (void                  *data,
                 uint32_t               id,
                 uint32_t               permissions G_GNUC_UNUSED,
                 const char            *type,
                 uint32_t               version G_GNUC_UNUSED,
                 const struct spa_dict *props)
{
  OozeEarPw *self = data;
  const char *mc;
  OozeEarNodeKind kind;
  EarNode *node;
  struct pw_proxy *proxy;
  const char *desc;

  if (g_strcmp0 (type, PW_TYPE_INTERFACE_Metadata) == 0)
    {
      bind_metadata_if_needed (self, id, type, props);
      return;
    }

  if (g_strcmp0 (type, PW_TYPE_INTERFACE_Node) != 0)
    return;

  mc = props ? spa_dict_lookup (props, PW_KEY_MEDIA_CLASS) : NULL;
  kind = kind_from_media_class (mc);
  if ((int) kind < 0)
    return;

  proxy = pw_registry_bind (self->registry, id, type, PW_VERSION_NODE, 0);
  if (!proxy)
    return;

  node = g_new0 (EarNode, 1);
  node->pw = self;
  node->id = id;
  node->kind = kind;
  node->proxy = proxy;
  node->volume = 1.f;
  node->channels = 2;
  node->mute = FALSE;

  desc = spa_dict_lookup (props, PW_KEY_NODE_DESCRIPTION);
  if (!desc)
    desc = spa_dict_lookup (props, PW_KEY_NODE_NAME);
  node->name = g_strdup (desc ? desc : "Audio");
  {
    const char *nn = spa_dict_lookup (props, PW_KEY_NODE_NAME);
    node->node_name = g_strdup (nn);
  }
  {
    const char *serial = spa_dict_lookup (props, PW_KEY_OBJECT_SERIAL);
    node->object_serial = g_strdup (serial);
  }

  pw_proxy_add_listener (proxy, &node->proxy_listener, &proxy_events, node);
  pw_proxy_add_object_listener (proxy, &node->object_listener,
                                &node_events, node);
  pw_node_enum_params ((struct pw_node *) proxy,
                       0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

  g_mutex_lock (&self->lock);
  g_hash_table_insert (self->nodes, GUINT_TO_POINTER (id), node);
  g_mutex_unlock (&self->lock);

  request_notify (self);
}

static void
registry_global_remove (void *data, uint32_t id)
{
  OozeEarPw *self = data;
  EarNode *node;

  g_mutex_lock (&self->lock);
  node = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (id));
  if (node)
    {
      /* Steal so free-func doesn't run while we destroy the proxy. */
      g_hash_table_steal (self->nodes, GUINT_TO_POINTER (id));
      if (node->proxy)
        {
          pw_proxy_destroy (node->proxy);
          node->proxy = NULL;
        }
      g_free (node->name);
      g_free (node->node_name);
      g_free (node->object_serial);
      g_free (node->detail);
      g_free (node->target);
      g_free (node);
    }
  g_mutex_unlock (&self->lock);

  request_notify (self);
}

static int
on_metadata_property (void       *data,
                      uint32_t    subject,
                      const char *key,
                      const char *type G_GNUC_UNUSED,
                      const char *value)
{
  OozeEarPw *self = data;
  char *name;

  if (!key)
    return 0;

  /* Per-stream routing: subject is the stream node id. */
  if (subject != PW_ID_CORE &&
      (g_strcmp0 (key, "target.object") == 0 ||
       g_strcmp0 (key, "target.node") == 0))
    {
      EarNode *node;

      g_mutex_lock (&self->lock);
      node = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (subject));
      if (node)
        {
          g_free (node->target);
          if (value && value[0] && g_strcmp0 (value, "-1") != 0)
            node->target = g_strdup (value);
          else
            node->target = NULL;
        }
      g_mutex_unlock (&self->lock);
      request_notify (self);
      return 0;
    }

  if (subject != PW_ID_CORE)
    return 0;

  name = json_get_name (value);

  if (g_strcmp0 (key, "default.audio.sink") == 0 ||
      g_strcmp0 (key, "default.configured.audio.sink") == 0)
    {
      g_mutex_lock (&self->lock);
      g_free (self->default_sink);
      self->default_sink = name;
      name = NULL;
      g_mutex_unlock (&self->lock);
      request_notify (self);
    }
  else if (g_strcmp0 (key, "default.audio.source") == 0 ||
           g_strcmp0 (key, "default.configured.audio.source") == 0)
    {
      g_mutex_lock (&self->lock);
      g_free (self->default_source);
      self->default_source = name;
      name = NULL;
      g_mutex_unlock (&self->lock);
      request_notify (self);
    }

  g_free (name);
  return 0;
}

static const struct pw_metadata_events metadata_events = {
  PW_VERSION_METADATA_EVENTS,
  .property = on_metadata_property,
};

static void
bind_metadata_if_needed (OozeEarPw             *self,
                         uint32_t               id,
                         const char            *type,
                         const struct spa_dict *props)
{
  const char *name;

  if (self->metadata)
    return;
  if (g_strcmp0 (type, PW_TYPE_INTERFACE_Metadata) != 0)
    return;

  name = props ? spa_dict_lookup (props, PW_KEY_METADATA_NAME) : NULL;
  if (name && g_strcmp0 (name, "default") != 0)
    return;

  self->metadata = pw_registry_bind (self->registry, id, type,
                                     PW_VERSION_METADATA, 0);
  if (!self->metadata)
    return;

  pw_proxy_add_object_listener (self->metadata, &self->metadata_listener,
                                &metadata_events, self);
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void
on_core_error (void       *data G_GNUC_UNUSED,
               uint32_t    id,
               int         seq G_GNUC_UNUSED,
               int         res,
               const char *message)
{
  g_warning ("Ooze Ear PipeWire error id=%u res=%d: %s",
             id, res, message ? message : "?");
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

/* ── Public API ──────────────────────────────────────────────────────────── */

OozeEarPw *
ooze_ear_pw_new (OozeEarPwChangedFunc changed, gpointer user_data)
{
  OozeEarPw *self;

  pw_init (NULL, NULL);

  self = g_new0 (OozeEarPw, 1);
  self->changed = changed;
  self->user_data = user_data;
  g_mutex_init (&self->lock);
  /* Values are freed manually on remove; destroy on table free. */
  self->nodes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                       NULL, ear_node_free);

  self->loop = pw_thread_loop_new ("ooze-ear", NULL);
  if (!self->loop)
    {
      g_warning ("Ooze Ear: failed to create PipeWire thread loop");
      ooze_ear_pw_free (self);
      return NULL;
    }

  self->context = pw_context_new (pw_thread_loop_get_loop (self->loop), NULL, 0);
  if (!self->context)
    {
      g_warning ("Ooze Ear: failed to create PipeWire context");
      ooze_ear_pw_free (self);
      return NULL;
    }

  pw_thread_loop_lock (self->loop);

  self->core = pw_context_connect (self->context, NULL, 0);
  if (!self->core)
    {
      pw_thread_loop_unlock (self->loop);
      g_warning ("Ooze Ear: could not connect to PipeWire");
      ooze_ear_pw_free (self);
      return NULL;
    }

  pw_core_add_listener (self->core, &self->core_listener, &core_events, self);
  self->registry = pw_core_get_registry (self->core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener (self->registry, &self->registry_listener,
                            &registry_events, self);

  pw_thread_loop_unlock (self->loop);
  pw_thread_loop_start (self->loop);
  return self;
}

void
ooze_ear_pw_free (OozeEarPw *self)
{
  if (!self)
    return;

  if (self->idle_id)
    {
      g_source_remove (self->idle_id);
      self->idle_id = 0;
    }

  if (self->loop)
    pw_thread_loop_stop (self->loop);

  if (self->loop)
    pw_thread_loop_lock (self->loop);

  if (self->metadata)
    {
      pw_proxy_destroy (self->metadata);
      self->metadata = NULL;
    }

  g_clear_pointer (&self->nodes, g_hash_table_unref);
  g_clear_pointer (&self->default_sink, g_free);
  g_clear_pointer (&self->default_source, g_free);

  if (self->registry)
    {
      pw_proxy_destroy ((struct pw_proxy *) self->registry);
      self->registry = NULL;
    }
  if (self->core)
    {
      pw_core_disconnect (self->core);
      self->core = NULL;
    }
  if (self->context)
    {
      pw_context_destroy (self->context);
      self->context = NULL;
    }

  if (self->loop)
    {
      pw_thread_loop_unlock (self->loop);
      pw_thread_loop_destroy (self->loop);
      self->loop = NULL;
    }

  g_mutex_clear (&self->lock);
  g_free (self);
  pw_deinit ();
}

static void
free_node_info (gpointer p)
{
  OozeEarNodeInfo *n = p;
  if (!n)
    return;
  g_free (n->name);
  g_free (n->node_name);
  g_free (n->object_serial);
  g_free (n->detail);
  g_free (n->target);
  g_free (n);
}

GPtrArray *
ooze_ear_pw_snapshot (OozeEarPw *self)
{
  GPtrArray *out;
  GHashTableIter iter;
  gpointer key, value;
  char *default_sink = NULL;
  char *default_source = NULL;

  out = g_ptr_array_new_with_free_func (free_node_info);
  if (!self)
    return out;

  g_mutex_lock (&self->lock);
  default_sink = g_strdup (self->default_sink);
  default_source = g_strdup (self->default_source);
  g_hash_table_iter_init (&iter, self->nodes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      EarNode *n = value;
      OozeEarNodeInfo *info = g_new0 (OozeEarNodeInfo, 1);

      info->id = n->id;
      info->kind = n->kind;
      info->name = g_strdup (n->name ? n->name : "Audio");
      info->node_name = g_strdup (n->node_name);
      info->object_serial = g_strdup (n->object_serial);
      info->detail = g_strdup (n->detail);
      info->target = g_strdup (n->target);
      info->volume = n->volume;
      info->mute = n->mute;
      info->channels = n->channels ? n->channels : 2;
      if (n->kind == OOZE_EAR_NODE_SINK && n->node_name && default_sink)
        info->is_default = (g_strcmp0 (n->node_name, default_sink) == 0);
      else if (n->kind == OOZE_EAR_NODE_SOURCE && n->node_name && default_source)
        info->is_default = (g_strcmp0 (n->node_name, default_source) == 0);
      g_ptr_array_add (out, info);
    }
  g_mutex_unlock (&self->lock);

  g_free (default_sink);
  g_free (default_source);
  return out;
}

void
ooze_ear_pw_free_nodes (GPtrArray *nodes)
{
  if (nodes)
    g_ptr_array_unref (nodes);
}

void
ooze_ear_pw_set_volume (OozeEarPw *self, uint32_t id, float volume)
{
  EarNode *n;
  uint8_t buffer[1024];
  struct spa_pod_builder b;
  float vols[8];
  guint i, ch;
  const struct spa_pod *param;

  if (!self)
    return;
  if (volume < 0.f)
    volume = 0.f;
  if (volume > 1.f)
    volume = 1.f;

  self->suppress_notify++;

  pw_thread_loop_lock (self->loop);
  g_mutex_lock (&self->lock);
  n = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (id));
  if (!n || !n->proxy)
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      self->suppress_notify--;
      return;
    }

  ch = n->channels ? n->channels : 2;
  if (ch > G_N_ELEMENTS (vols))
    ch = G_N_ELEMENTS (vols);
  for (i = 0; i < ch; i++)
    vols[i] = volume;

  spa_pod_builder_init (&b, buffer, sizeof (buffer));
  param = spa_pod_builder_add_object (
      &b,
      SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
      SPA_PROP_channelVolumes, SPA_POD_Array (sizeof (float), SPA_TYPE_Float, ch, vols),
      SPA_PROP_softVolumes,    SPA_POD_Array (sizeof (float), SPA_TYPE_Float, ch, vols));

  pw_node_set_param ((struct pw_node *) n->proxy, SPA_PARAM_Props, 0, param);
  n->volume = volume;
  g_mutex_unlock (&self->lock);
  pw_thread_loop_unlock (self->loop);

  self->suppress_notify--;
}

void
ooze_ear_pw_set_mute (OozeEarPw *self, uint32_t id, gboolean mute)
{
  EarNode *n;
  uint8_t buffer[512];
  struct spa_pod_builder b;
  const struct spa_pod *param;

  if (!self)
    return;

  self->suppress_notify++;

  pw_thread_loop_lock (self->loop);
  g_mutex_lock (&self->lock);
  n = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (id));
  if (!n || !n->proxy)
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      self->suppress_notify--;
      return;
    }

  spa_pod_builder_init (&b, buffer, sizeof (buffer));
  param = spa_pod_builder_add_object (
      &b,
      SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
      SPA_PROP_mute, SPA_POD_Bool (!!mute),
      SPA_PROP_softMute, SPA_POD_Bool (!!mute));

  pw_node_set_param ((struct pw_node *) n->proxy, SPA_PARAM_Props, 0, param);
  n->mute = mute;
  g_mutex_unlock (&self->lock);
  pw_thread_loop_unlock (self->loop);

  self->suppress_notify--;
}

void
ooze_ear_pw_set_default (OozeEarPw *self, uint32_t id)
{
  EarNode *n;
  const char *key_configured;
  const char *key_current;
  g_autofree char *json = NULL;
  g_autofree char *node_name = NULL;

  if (!self)
    return;

  pw_thread_loop_lock (self->loop);
  g_mutex_lock (&self->lock);
  n = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (id));
  if (!n || !n->node_name || !self->metadata)
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      return;
    }

  if (n->kind == OOZE_EAR_NODE_SINK)
    {
      key_configured = "default.configured.audio.sink";
      key_current = "default.audio.sink";
    }
  else if (n->kind == OOZE_EAR_NODE_SOURCE)
    {
      key_configured = "default.configured.audio.source";
      key_current = "default.audio.source";
    }
  else
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      return;
    }

  node_name = g_strdup (n->node_name);
  json = g_strdup_printf ("{\"name\":\"%s\"}", node_name);

  /* WirePlumber persists / honors the configured key; also set current. */
  pw_metadata_set_property ((struct pw_metadata *) self->metadata,
                            PW_ID_CORE, key_configured, "Spa:String:JSON", json);
  pw_metadata_set_property ((struct pw_metadata *) self->metadata,
                            PW_ID_CORE, key_current, "Spa:String:JSON", json);

  if (n->kind == OOZE_EAR_NODE_SINK)
    {
      g_free (self->default_sink);
      self->default_sink = g_strdup (node_name);
    }
  else
    {
      g_free (self->default_source);
      self->default_source = g_strdup (node_name);
    }

  g_mutex_unlock (&self->lock);
  pw_thread_loop_unlock (self->loop);
  request_notify (self);
}

void
ooze_ear_pw_set_target (OozeEarPw *self, uint32_t stream_id, uint32_t target_id)
{
  EarNode *stream;
  EarNode *target = NULL;
  g_autofree char *serial = NULL;

  if (!self)
    return;

  pw_thread_loop_lock (self->loop);
  g_mutex_lock (&self->lock);

  stream = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (stream_id));
  if (!stream || !self->metadata)
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      return;
    }

  if (stream->kind != OOZE_EAR_NODE_PLAYBACK &&
      stream->kind != OOZE_EAR_NODE_RECORD)
    {
      g_mutex_unlock (&self->lock);
      pw_thread_loop_unlock (self->loop);
      return;
    }

  if (target_id != 0)
    {
      target = g_hash_table_lookup (self->nodes, GUINT_TO_POINTER (target_id));
      if (!target)
        {
          g_mutex_unlock (&self->lock);
          pw_thread_loop_unlock (self->loop);
          return;
        }

      if (stream->kind == OOZE_EAR_NODE_PLAYBACK &&
          target->kind != OOZE_EAR_NODE_SINK)
        {
          g_mutex_unlock (&self->lock);
          pw_thread_loop_unlock (self->loop);
          return;
        }
      if (stream->kind == OOZE_EAR_NODE_RECORD &&
          target->kind != OOZE_EAR_NODE_SOURCE)
        {
          g_mutex_unlock (&self->lock);
          pw_thread_loop_unlock (self->loop);
          return;
        }

      /* Prefer object.serial (Spa:Id) — what WirePlumber expects. */
      if (target->object_serial)
        serial = g_strdup (target->object_serial);
      else if (target->node_name)
        serial = g_strdup (target->node_name);
      else
        serial = g_strdup_printf ("%u", target_id);
    }

  if (serial)
    {
      pw_metadata_set_property ((struct pw_metadata *) self->metadata,
                                stream_id, "target.object", "Spa:Id", serial);
      g_free (stream->target);
      stream->target = g_strdup (serial);
    }
  else
    {
      /* -1 clears a forced target so the stream follows the default device. */
      pw_metadata_set_property ((struct pw_metadata *) self->metadata,
                                stream_id, "target.object", "Spa:Id", "-1");
      g_free (stream->target);
      stream->target = NULL;
    }

  g_mutex_unlock (&self->lock);
  pw_thread_loop_unlock (self->loop);
  request_notify (self);
}
