#include "ooze-appmenu-wayland.h"

#include <wayland-server-core.h>

#include <meta/meta-context.h>
#include <meta/meta-wayland-compositor.h>

#include "appmenu-server-protocol.h"

#define OOZE_APPMENU_MANAGER_VERSION 2

typedef struct
{
  OozeAppmenuWayland *owner;
  struct wl_resource *resource;         /* org_kde_kwin_appmenu */
  struct wl_resource *surface_resource; /* wl_surface (non-owning) */
  pid_t               pid;
  char               *service;
  char               *path;
  gint64              stamp; /* monotonic; newest wins on pid ties */
} OozeAppmenuEntry;

struct _OozeAppmenuWayland
{
  MetaDisplay *display;
  struct wl_global *global;
  GPtrArray *entries; /* OozeAppmenuEntry*, owned */
  guint changed_idle;

  OozeAppmenuWaylandChangedFunc changed_cb;
  gpointer                      changed_data;
};

static void
ooze_appmenu_entry_free (gpointer data)
{
  OozeAppmenuEntry *entry = data;

  g_free (entry->service);
  g_free (entry->path);
  g_free (entry);
}

static gboolean
ooze_appmenu_changed_idle (gpointer user_data)
{
  OozeAppmenuWayland *self = user_data;

  self->changed_idle = 0;
  if (self->changed_cb)
    self->changed_cb (self->changed_data);
  return G_SOURCE_REMOVE;
}

static void
ooze_appmenu_schedule_changed (OozeAppmenuWayland *self)
{
  if (self->changed_idle != 0)
    return;
  self->changed_idle = g_idle_add (ooze_appmenu_changed_idle, self);
}

/* ── org_kde_kwin_appmenu ────────────────────────────────────────────────── */

static void
appmenu_set_address (struct wl_client   *client G_GNUC_UNUSED,
                     struct wl_resource *resource,
                     const char         *service_name,
                     const char         *object_path)
{
  OozeAppmenuEntry *entry = wl_resource_get_user_data (resource);

  if (!entry)
    return;

  g_free (entry->service);
  g_free (entry->path);
  entry->service = g_strdup (service_name);
  entry->path = g_strdup (object_path);
  entry->stamp = g_get_monotonic_time ();

  g_print ("Ooze appmenu (wayland): pid=%d announced %s %s\n",
           (int) entry->pid,
           service_name ? service_name : "?",
           object_path ? object_path : "?");

  ooze_appmenu_schedule_changed (entry->owner);
}

static void
appmenu_release (struct wl_client   *client G_GNUC_UNUSED,
                 struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct org_kde_kwin_appmenu_interface appmenu_impl = {
  .set_address = appmenu_set_address,
  .release = appmenu_release,
};

static void
appmenu_resource_destroyed (struct wl_resource *resource)
{
  OozeAppmenuEntry *entry = wl_resource_get_user_data (resource);
  OozeAppmenuWayland *self;

  if (!entry)
    return;

  self = entry->owner;
  g_ptr_array_remove (self->entries, entry);
  ooze_appmenu_schedule_changed (self);
}

/* ── org_kde_kwin_appmenu_manager ────────────────────────────────────────── */

static void
appmenu_manager_create (struct wl_client   *client,
                        struct wl_resource *manager_resource,
                        uint32_t            id,
                        struct wl_resource *surface_resource)
{
  OozeAppmenuWayland *self = wl_resource_get_user_data (manager_resource);
  OozeAppmenuEntry *entry;
  struct wl_resource *resource;
  pid_t pid = 0;

  resource = wl_resource_create (client,
                                 &org_kde_kwin_appmenu_interface,
                                 wl_resource_get_version (manager_resource),
                                 id);
  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_client_get_credentials (client, &pid, NULL, NULL);

  entry = g_new0 (OozeAppmenuEntry, 1);
  entry->owner = self;
  entry->resource = resource;
  entry->surface_resource = surface_resource;
  entry->pid = pid;
  g_ptr_array_add (self->entries, entry);

  wl_resource_set_implementation (resource, &appmenu_impl,
                                  entry, appmenu_resource_destroyed);
}

static void
appmenu_manager_release (struct wl_client   *client G_GNUC_UNUSED,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct org_kde_kwin_appmenu_manager_interface appmenu_manager_impl = {
  .create = appmenu_manager_create,
  .release = appmenu_manager_release,
};

static void
appmenu_manager_bind (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &org_kde_kwin_appmenu_manager_interface,
                                 (int) version, id);
  if (!resource)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource, &appmenu_manager_impl, data, NULL);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

OozeAppmenuWayland *
ooze_appmenu_wayland_new (MetaDisplay *display)
{
  OozeAppmenuWayland *self;
  MetaContext *context;
  MetaWaylandCompositor *compositor;
  struct wl_display *wl_display;

  context = meta_display_get_context (display);
  compositor = meta_context_get_wayland_compositor (context);
  if (!compositor)
    return NULL;

  wl_display = meta_wayland_compositor_get_wayland_display (compositor);
  if (!wl_display)
    return NULL;

  self = g_new0 (OozeAppmenuWayland, 1);
  self->display = display;
  self->entries = g_ptr_array_new_with_free_func (ooze_appmenu_entry_free);
  self->global = wl_global_create (wl_display,
                                   &org_kde_kwin_appmenu_manager_interface,
                                   OOZE_APPMENU_MANAGER_VERSION,
                                   self, appmenu_manager_bind);
  if (!self->global)
    {
      g_ptr_array_unref (self->entries);
      g_free (self);
      return NULL;
    }

  g_print ("Ooze appmenu (wayland): org_kde_kwin_appmenu_manager v%d up\n",
           OOZE_APPMENU_MANAGER_VERSION);
  return self;
}

void
ooze_appmenu_wayland_free (OozeAppmenuWayland *self)
{
  guint i;

  if (!self)
    return;

  if (self->changed_idle)
    {
      g_source_remove (self->changed_idle);
      self->changed_idle = 0;
    }

  /* Detach live resources so late destroy callbacks don't touch us. */
  for (i = 0; i < self->entries->len; i++)
    {
      OozeAppmenuEntry *entry = g_ptr_array_index (self->entries, i);

      wl_resource_set_user_data (entry->resource, NULL);
    }

  g_clear_pointer (&self->global, wl_global_destroy);
  g_ptr_array_unref (self->entries);
  g_free (self);
}

void
ooze_appmenu_wayland_set_changed_callback (OozeAppmenuWayland            *self,
                                           OozeAppmenuWaylandChangedFunc  callback,
                                           gpointer                       user_data)
{
  if (!self)
    return;
  self->changed_cb = callback;
  self->changed_data = user_data;
}

/*
 * Public API only exposes MetaWindow, not its wl_surface, so entries are
 * matched by client pid; the newest announcement for that pid wins.
 */
gboolean
ooze_appmenu_wayland_lookup (OozeAppmenuWayland  *self,
                             MetaWindow          *window,
                             const char         **service_out,
                             const char         **path_out)
{
  OozeAppmenuEntry *best = NULL;
  pid_t pid;
  guint i;

  if (!self || !window)
    return FALSE;

  pid = meta_window_get_pid (window);
  if (pid <= 0)
    return FALSE;

  for (i = 0; i < self->entries->len; i++)
    {
      OozeAppmenuEntry *entry = g_ptr_array_index (self->entries, i);

      if (entry->pid != pid || !entry->service || !entry->path)
        continue;
      if (!best || entry->stamp > best->stamp)
        best = entry;
    }

  if (!best)
    return FALSE;

  if (service_out)
    *service_out = best->service;
  if (path_out)
    *path_out = best->path;
  return TRUE;
}
