#include "ooze-sni.h"

#include "ooze-theme.h"

#include "../shared/ooze-icon-lookup.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#define OOZE_SNI_WATCHER_NAME      "org.kde.StatusNotifierWatcher"
#define OOZE_SNI_WATCHER_PATH      "/StatusNotifierWatcher"
#define OOZE_SNI_WATCHER_IFACE     "org.kde.StatusNotifierWatcher"
#define OOZE_SNI_ITEM_IFACE        "org.kde.StatusNotifierItem"
#define OOZE_SNI_DEFAULT_ITEM_PATH "/StatusNotifierItem"
#define OOZE_SNI_ICON_MAX_PX       64
#define OOZE_SNI_ICON_DISPLAY_PX   20

struct _OozeSniItem
{
  char *bus_name;
  char *object_path;
  char *service_key; /* RegisteredStatusNotifierItems entry */

  char *id;
  char *title;
  char *icon_name;
  char *icon_theme_path;
  char *menu_path;
  gboolean item_is_menu;
  gboolean icon_from_name; /* TRUE when icon_pixbuf came from IconName */
  GdkPixbuf *icon_pixbuf;

  GDBusConnection *session;
  guint props_changed_id;
  guint name_watch_id;
  guint refresh_idle;

  OozeSniItemChangedFn changed_cb;
  OozeSniItemRemovedFn removed_cb; /* used when name vanishes */
  gpointer user_data;
  gpointer watcher; /* OozeSniWatcher* for vanish cleanup */
};

struct _OozeSniWatcher
{
  GDBusConnection *session;
  guint own_name_id;
  guint reg_id;
  GDBusNodeInfo *introspection;
  GHashTable *items; /* service_key -> OozeSniItem* */

  OozeSniItemChangedFn changed_cb;
  OozeSniItemRemovedFn removed_cb;
  gpointer user_data;
};

static void ooze_sni_item_refresh_async (OozeSniItem *item);
static void ooze_sni_watcher_remove_item (OozeSniWatcher *watcher,
                                          const char     *service_key);

/* ── Item accessors ──────────────────────────────────────────────────────── */

const char *
ooze_sni_item_bus_name (OozeSniItem *item)
{
  return item ? item->bus_name : NULL;
}

const char *
ooze_sni_item_object_path (OozeSniItem *item)
{
  return item ? item->object_path : NULL;
}

const char *
ooze_sni_item_id (OozeSniItem *item)
{
  return item ? item->id : NULL;
}

const char *
ooze_sni_item_title (OozeSniItem *item)
{
  return item ? item->title : NULL;
}

const char *
ooze_sni_item_icon_name (OozeSniItem *item)
{
  return item ? item->icon_name : NULL;
}

const char *
ooze_sni_item_menu_path (OozeSniItem *item)
{
  return item ? item->menu_path : NULL;
}

gboolean
ooze_sni_item_is_menu (OozeSniItem *item)
{
  return item && item->item_is_menu;
}

GdkPixbuf *
ooze_sni_item_icon_pixbuf (OozeSniItem *item)
{
  return item ? item->icon_pixbuf : NULL;
}

static void
ooze_sni_item_call (OozeSniItem *item,
                    const char  *method,
                    int          x,
                    int          y)
{
  if (!item || !item->session)
    return;

  g_dbus_connection_call (item->session,
                          item->bus_name,
                          item->object_path,
                          OOZE_SNI_ITEM_IFACE,
                          method,
                          g_variant_new ("(ii)", x, y),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          NULL,
                          NULL);
}

void
ooze_sni_item_activate (OozeSniItem *item, int x, int y)
{
  ooze_sni_item_call (item, "Activate", x, y);
}

void
ooze_sni_item_secondary_activate (OozeSniItem *item, int x, int y)
{
  ooze_sni_item_call (item, "SecondaryActivate", x, y);
}

void
ooze_sni_item_context_menu (OozeSniItem *item, int x, int y)
{
  ooze_sni_item_call (item, "ContextMenu", x, y);
}

/* ── IconPixmap a(iiay) ──────────────────────────────────────────────────── */

static void
ooze_sni_pixbuf_free_pixels (guchar *pixels, gpointer data G_GNUC_UNUSED)
{
  g_free (pixels);
}

static GdkPixbuf *
ooze_sni_pixbuf_from_icon_pixmap (GVariant *pixmap_v)
{
  GVariantIter iter;
  gint32 width = 0;
  gint32 height = 0;
  GVariant *bytes_v = NULL;
  gint32 best_w = 0;
  gint32 best_h = 0;
  g_autofree guint8 *best_copy = NULL;
  gsize best_len = 0;
  gsize n_pixels;
  guint8 *rgba;
  gsize i;

  if (!pixmap_v)
    return NULL;

  g_variant_iter_init (&iter, pixmap_v);
  while (g_variant_iter_next (&iter, "(ii@ay)", &width, &height, &bytes_v))
    {
      const guint8 *data;
      gsize len = 0;

      if (width < 1 || height < 1 || width > OOZE_SNI_ICON_MAX_PX ||
          height > OOZE_SNI_ICON_MAX_PX)
        {
          g_variant_unref (bytes_v);
          continue;
        }

      data = g_variant_get_fixed_array (bytes_v, &len, sizeof (guint8));
      n_pixels = (gsize) width * (gsize) height;
      if (!data || len < n_pixels * 4)
        {
          g_variant_unref (bytes_v);
          continue;
        }

      if (!best_copy ||
          ABS (width - OOZE_SNI_ICON_DISPLAY_PX) < ABS (best_w - OOZE_SNI_ICON_DISPLAY_PX))
        {
          g_free (best_copy);
          best_w = width;
          best_h = height;
          best_len = n_pixels * 4;
          best_copy = g_memdup2 (data, best_len);
        }
      g_variant_unref (bytes_v);
    }

  if (!best_copy || best_w < 1 || best_h < 1)
    return NULL;

  n_pixels = (gsize) best_w * (gsize) best_h;
  rgba = g_malloc (n_pixels * 4);
  for (i = 0; i < n_pixels; i++)
    {
      const guint8 *p = best_copy + i * 4;
      rgba[i * 4 + 0] = p[1];
      rgba[i * 4 + 1] = p[2];
      rgba[i * 4 + 2] = p[3];
      rgba[i * 4 + 3] = p[0];
    }

  return gdk_pixbuf_new_from_data (rgba,
                                   GDK_COLORSPACE_RGB,
                                   TRUE,
                                   8,
                                   best_w,
                                   best_h,
                                   best_w * 4,
                                   ooze_sni_pixbuf_free_pixels,
                                   NULL);
}

static gboolean
ooze_sni_item_emit_changed_idle (gpointer user_data)
{
  OozeSniItem *item = user_data;

  item->refresh_idle = 0;
  if (item->changed_cb)
    item->changed_cb (item, item->user_data);
  return G_SOURCE_REMOVE;
}

static void
ooze_sni_item_schedule_changed (OozeSniItem *item)
{
  if (!item || item->refresh_idle)
    return;
  item->refresh_idle = g_idle_add (ooze_sni_item_emit_changed_idle, item);
}

static gboolean
ooze_sni_icon_name_is_symbolic (const char *name)
{
  return name && g_str_has_suffix (name, "-symbolic");
}

/* Replace RGB with panel foreground; keep alpha (GNOME/KDE symbolic contract). */
static GdkPixbuf *
ooze_sni_pixbuf_tint_symbolic (GdkPixbuf *src)
{
  const OozeAquaPalette *palette;
  GdkPixbuf *work;
  guint8 *pixels;
  int width, height, rowstride, n_channels;
  guint8 fr, fg, fb;
  int y;

  if (!src)
    return NULL;

  palette = ooze_theme_get_palette (NULL);
  fr = (guint8) CLAMP (palette->menu_text_r * 255.0 + 0.5, 0.0, 255.0);
  fg = (guint8) CLAMP (palette->menu_text_g * 255.0 + 0.5, 0.0, 255.0);
  fb = (guint8) CLAMP (palette->menu_text_b * 255.0 + 0.5, 0.0, 255.0);

  if (gdk_pixbuf_get_has_alpha (src))
    work = gdk_pixbuf_copy (src);
  else
    work = gdk_pixbuf_add_alpha (src, FALSE, 0, 0, 0);
  if (!work)
    return NULL;

  pixels = gdk_pixbuf_get_pixels (work);
  width = gdk_pixbuf_get_width (work);
  height = gdk_pixbuf_get_height (work);
  rowstride = gdk_pixbuf_get_rowstride (work);
  n_channels = gdk_pixbuf_get_n_channels (work);
  if (n_channels < 4)
    return work;

  for (y = 0; y < height; y++)
    {
      guint8 *row = pixels + y * rowstride;
      int x;

      for (x = 0; x < width; x++)
        {
          guint8 *p = row + x * n_channels;
          p[0] = fr;
          p[1] = fg;
          p[2] = fb;
        }
    }

  return work;
}

/* IconThemePath is a freedesktop theme root: the icon usually lives in a
 * size/context subdirectory, so search the tree (bounded depth). */
static GdkPixbuf *
ooze_sni_load_from_theme_path (const char *dir,
                               const char *icon_name,
                               int         depth)
{
  static const char *exts[] = { "", ".png", ".svg", ".xpm" };
  g_autoptr (GDir) gdir = NULL;
  GdkPixbuf *pb = NULL;
  const char *entry;

  for (gsize i = 0; i < G_N_ELEMENTS (exts); i++)
    {
      g_autofree char *base = g_strconcat (icon_name, exts[i], NULL);
      g_autofree char *file = g_build_filename (dir, base, NULL);

      if (g_file_test (file, G_FILE_TEST_IS_REGULAR))
        {
          pb = gdk_pixbuf_new_from_file_at_size (file,
                                                 OOZE_SNI_ICON_DISPLAY_PX,
                                                 OOZE_SNI_ICON_DISPLAY_PX,
                                                 NULL);
          if (pb)
            return pb;
        }
    }

  if (depth <= 0)
    return NULL;

  gdir = g_dir_open (dir, 0, NULL);
  if (!gdir)
    return NULL;

  while ((entry = g_dir_read_name (gdir)))
    {
      g_autofree char *sub = g_build_filename (dir, entry, NULL);

      if (!g_file_test (sub, G_FILE_TEST_IS_DIR))
        continue;
      pb = ooze_sni_load_from_theme_path (sub, icon_name, depth - 1);
      if (pb)
        return pb;
    }

  return NULL;
}

static GdkPixbuf *
ooze_sni_item_load_named_icon (OozeSniItem *item)
{
  GdkPixbuf *pb = NULL;

  if (!item || !item->icon_name || !item->icon_name[0])
    return NULL;

  if (item->icon_theme_path && item->icon_theme_path[0])
    pb = ooze_sni_load_from_theme_path (item->icon_theme_path,
                                        item->icon_name, 4);

  if (!pb)
    pb = ooze_icon_lookup_load (item->icon_name, OOZE_SNI_ICON_DISPLAY_PX);

  if (pb && ooze_sni_icon_name_is_symbolic (item->icon_name))
    {
      GdkPixbuf *tinted = ooze_sni_pixbuf_tint_symbolic (pb);
      g_object_unref (pb);
      pb = tinted;
    }

  return pb;
}

void
ooze_sni_item_reresolve_icon (OozeSniItem *item)
{
  GdkPixbuf *pb;

  if (!item || !item->icon_name || !item->icon_name[0])
    return;

  pb = ooze_sni_item_load_named_icon (item);
  if (!pb)
    return;

  g_clear_object (&item->icon_pixbuf);
  item->icon_pixbuf = pb;
  item->icon_from_name = TRUE;
  ooze_sni_item_schedule_changed (item);
}

static void
ooze_sni_item_apply_props (OozeSniItem *item, GVariant *dict)
{
  const char *s;
  gboolean b;
  GVariant *pix;
  GdkPixbuf *named = NULL;
  GdkPixbuf *pixmap = NULL;

  if (!item || !dict)
    return;

  if (g_variant_lookup (dict, "Id", "&s", &s))
    {
      g_free (item->id);
      item->id = g_strdup (s);
    }
  if (g_variant_lookup (dict, "Title", "&s", &s))
    {
      g_free (item->title);
      item->title = g_strdup (s);
    }
  if (g_variant_lookup (dict, "IconName", "&s", &s))
    {
      g_free (item->icon_name);
      item->icon_name = g_strdup (s);
    }
  if (g_variant_lookup (dict, "IconThemePath", "&s", &s))
    {
      g_free (item->icon_theme_path);
      item->icon_theme_path = g_strdup (s);
    }
  if (g_variant_lookup (dict, "Menu", "&o", &s) ||
      g_variant_lookup (dict, "Menu", "&s", &s))
    {
      g_free (item->menu_path);
      item->menu_path = g_strdup (s);
    }
  if (g_variant_lookup (dict, "ItemIsMenu", "b", &b))
    item->item_is_menu = b;

  /* Prefer IconName over IconPixmap when both are present (themeable hosts). */
  named = ooze_sni_item_load_named_icon (item);

  pix = g_variant_lookup_value (dict, "IconPixmap", G_VARIANT_TYPE ("a(iiay)"));
  if (pix)
    {
      pixmap = ooze_sni_pixbuf_from_icon_pixmap (pix);
      g_variant_unref (pix);
    }

  g_clear_object (&item->icon_pixbuf);
  if (named)
    {
      item->icon_pixbuf = named;
      item->icon_from_name = TRUE;
      g_clear_object (&pixmap);
    }
  else if (pixmap)
    {
      item->icon_pixbuf = pixmap;
      item->icon_from_name = FALSE;
    }
  else
    {
      item->icon_from_name = FALSE;
    }

  ooze_sni_item_schedule_changed (item);
}

static void
ooze_sni_item_get_all_done (GObject      *source,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  OozeSniItem *item = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) dict = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
  if (!reply)
    {
      g_warning ("Ooze SNI: GetAll(%s %s) failed: %s",
                 item->bus_name, item->object_path, error->message);
      return;
    }

  dict = g_variant_get_child_value (reply, 0);
  ooze_sni_item_apply_props (item, dict);
}

static void
ooze_sni_item_refresh_async (OozeSniItem *item)
{
  if (!item || !item->session)
    return;

  g_dbus_connection_call (item->session,
                          item->bus_name,
                          item->object_path,
                          "org.freedesktop.DBus.Properties",
                          "GetAll",
                          g_variant_new ("(s)", OOZE_SNI_ITEM_IFACE),
                          G_VARIANT_TYPE ("(a{sv})"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2500,
                          NULL,
                          ooze_sni_item_get_all_done,
                          item);
}

static void
ooze_sni_item_on_signal (GDBusConnection *connection G_GNUC_UNUSED,
                         const char      *sender G_GNUC_UNUSED,
                         const char      *object_path G_GNUC_UNUSED,
                         const char      *interface_name G_GNUC_UNUSED,
                         const char      *signal_name,
                         GVariant        *parameters G_GNUC_UNUSED,
                         gpointer         user_data)
{
  OozeSniItem *item = user_data;

  if (g_strcmp0 (signal_name, "NewIcon") == 0 ||
      g_strcmp0 (signal_name, "NewAttentionIcon") == 0 ||
      g_strcmp0 (signal_name, "NewStatus") == 0 ||
      g_strcmp0 (signal_name, "NewTitle") == 0 ||
      g_strcmp0 (signal_name, "NewMenu") == 0 ||
      g_strcmp0 (signal_name, "NewIconThemePath") == 0)
    ooze_sni_item_refresh_async (item);
}

static void
ooze_sni_item_free (OozeSniItem *item)
{
  if (!item)
    return;

  if (item->refresh_idle)
    {
      g_source_remove (item->refresh_idle);
      item->refresh_idle = 0;
    }
  if (item->session && item->props_changed_id)
    g_dbus_connection_signal_unsubscribe (item->session, item->props_changed_id);
  if (item->name_watch_id)
    g_bus_unwatch_name (item->name_watch_id);

  g_clear_object (&item->icon_pixbuf);
  g_free (item->bus_name);
  g_free (item->object_path);
  g_free (item->service_key);
  g_free (item->id);
  g_free (item->title);
  g_free (item->icon_name);
  g_free (item->icon_theme_path);
  g_free (item->menu_path);
  g_free (item);
}

static void
ooze_sni_item_name_vanished (GDBusConnection *connection G_GNUC_UNUSED,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
  OozeSniItem *item = user_data;
  OozeSniWatcher *watcher = item ? item->watcher : NULL;

  if (!item || !watcher || !item->service_key)
    return;
  ooze_sni_watcher_remove_item (watcher, item->service_key);
}

static OozeSniItem *
ooze_sni_item_new (GDBusConnection     *session,
                   OozeSniWatcher      *watcher,
                   const char          *bus_name,
                   const char          *object_path,
                   const char          *service_key,
                   OozeSniItemChangedFn changed_cb,
                   gpointer             user_data)
{
  OozeSniItem *item;

  item = g_new0 (OozeSniItem, 1);
  item->session = session;
  item->watcher = watcher;
  item->bus_name = g_strdup (bus_name);
  item->object_path = g_strdup (object_path);
  item->service_key = g_strdup (service_key);
  item->changed_cb = changed_cb;
  item->user_data = user_data;
  item->item_is_menu = FALSE;

  item->props_changed_id =
    g_dbus_connection_signal_subscribe (session,
                                        bus_name,
                                        OOZE_SNI_ITEM_IFACE,
                                        NULL,
                                        object_path,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        ooze_sni_item_on_signal,
                                        item,
                                        NULL);

  item->name_watch_id =
    g_bus_watch_name_on_connection (session,
                                    bus_name,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    NULL,
                                    ooze_sni_item_name_vanished,
                                    item,
                                    NULL);

  ooze_sni_item_refresh_async (item);
  return item;
}

/* ── Watcher ─────────────────────────────────────────────────────────────── */

static const char ooze_sni_watcher_xml[] =
  "<node>"
  "  <interface name='org.kde.StatusNotifierWatcher'>"
  "    <method name='RegisterStatusNotifierItem'>"
  "      <arg type='s' name='service' direction='in'/>"
  "    </method>"
  "    <method name='RegisterStatusNotifierHost'>"
  "      <arg type='s' name='service' direction='in'/>"
  "    </method>"
  "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
  "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
  "    <property name='ProtocolVersion' type='i' access='read'/>"
  "    <signal name='StatusNotifierItemRegistered'>"
  "      <arg type='s' name='service'/>"
  "    </signal>"
  "    <signal name='StatusNotifierItemUnregistered'>"
  "      <arg type='s' name='service'/>"
  "    </signal>"
  "    <signal name='StatusNotifierHostRegistered'/>"
  "    <signal name='StatusNotifierHostUnregistered'/>"
  "  </interface>"
  "</node>";

static void
ooze_sni_watcher_emit (OozeSniWatcher *watcher,
                       const char     *signal_name,
                       GVariant       *params)
{
  if (!watcher || !watcher->session)
    return;

  g_dbus_connection_emit_signal (watcher->session,
                                 NULL,
                                 OOZE_SNI_WATCHER_PATH,
                                 OOZE_SNI_WATCHER_IFACE,
                                 signal_name,
                                 params,
                                 NULL);
}

static void
ooze_sni_watcher_remove_item (OozeSniWatcher *watcher,
                              const char     *service_key)
{
  OozeSniItem *item;

  if (!watcher || !service_key)
    return;

  item = g_hash_table_lookup (watcher->items, service_key);
  if (!item)
    return;

  g_hash_table_steal (watcher->items, service_key);
  if (watcher->removed_cb)
    watcher->removed_cb (item, watcher->user_data);
  ooze_sni_watcher_emit (watcher,
                         "StatusNotifierItemUnregistered",
                         g_variant_new ("(s)", service_key));
  ooze_sni_item_free (item);
}

static void
ooze_sni_watcher_add_item (OozeSniWatcher *watcher,
                           const char     *sender,
                           const char     *service)
{
  g_autofree char *bus_name = NULL;
  g_autofree char *object_path = NULL;
  g_autofree char *service_key = NULL;
  OozeSniItem *item;

  if (!service || !service[0])
    return;

  if (service[0] == '/')
    {
      /* Ayatana: object path; bus name is the caller. */
      bus_name = g_strdup (sender);
      object_path = g_strdup (service);
    }
  else if (strchr (service, '/'))
    {
      const char *slash = strchr (service, '/');
      bus_name = g_strndup (service, (gsize) (slash - service));
      object_path = g_strdup (slash);
    }
  else
    {
      bus_name = g_strdup (service);
      object_path = g_strdup (OOZE_SNI_DEFAULT_ITEM_PATH);
    }

  service_key = g_strdup_printf ("%s%s", bus_name, object_path);

  if (g_hash_table_contains (watcher->items, service_key))
    {
      item = g_hash_table_lookup (watcher->items, service_key);
      ooze_sni_item_refresh_async (item);
      return;
    }

  item = ooze_sni_item_new (watcher->session,
                            watcher,
                            bus_name,
                            object_path,
                            service_key,
                            watcher->changed_cb,
                            watcher->user_data);

  g_hash_table_insert (watcher->items, g_strdup (service_key), item);
  ooze_sni_watcher_emit (watcher,
                         "StatusNotifierItemRegistered",
                         g_variant_new ("(s)", service_key));
  if (watcher->changed_cb)
    watcher->changed_cb (item, watcher->user_data);
}

static GVariant *
ooze_sni_watcher_registered_items (OozeSniWatcher *watcher)
{
  GVariantBuilder builder;
  GHashTableIter iter;
  gpointer key;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
  g_hash_table_iter_init (&iter, watcher->items);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    g_variant_builder_add (&builder, "s", (const char *) key);
  return g_variant_builder_end (&builder);
}

static void
ooze_sni_watcher_method_call (GDBusConnection       *connection G_GNUC_UNUSED,
                              const char            *sender,
                              const char            *object_path G_GNUC_UNUSED,
                              const char            *interface_name G_GNUC_UNUSED,
                              const char            *method_name,
                              GVariant              *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer               user_data)
{
  OozeSniWatcher *watcher = user_data;

  if (g_strcmp0 (method_name, "RegisterStatusNotifierItem") == 0)
    {
      const char *service = NULL;
      g_variant_get (parameters, "(&s)", &service);
      ooze_sni_watcher_add_item (watcher, sender, service);
      g_dbus_method_invocation_return_value (invocation, NULL);
      return;
    }

  if (g_strcmp0 (method_name, "RegisterStatusNotifierHost") == 0)
    {
      /* We are the host; accept no-op so clients that call this don't fail. */
      g_dbus_method_invocation_return_value (invocation, NULL);
      return;
    }

  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_METHOD,
                                         "Unknown method %s",
                                         method_name);
}

static GVariant *
ooze_sni_watcher_get_property (GDBusConnection *connection G_GNUC_UNUSED,
                               const char      *sender G_GNUC_UNUSED,
                               const char      *object_path G_GNUC_UNUSED,
                               const char      *interface_name G_GNUC_UNUSED,
                               const char      *property_name,
                               GError         **error G_GNUC_UNUSED,
                               gpointer         user_data)
{
  OozeSniWatcher *watcher = user_data;

  if (g_strcmp0 (property_name, "RegisteredStatusNotifierItems") == 0)
    return ooze_sni_watcher_registered_items (watcher);
  if (g_strcmp0 (property_name, "IsStatusNotifierHostRegistered") == 0)
    return g_variant_new_boolean (TRUE);
  if (g_strcmp0 (property_name, "ProtocolVersion") == 0)
    return g_variant_new_int32 (0);
  return NULL;
}

static const GDBusInterfaceVTable ooze_sni_watcher_vtable = {
  .method_call = ooze_sni_watcher_method_call,
  .get_property = ooze_sni_watcher_get_property,
  .set_property = NULL,
};

static void
ooze_sni_watcher_on_bus_acquired (GDBusConnection *connection,
                                  const char      *name G_GNUC_UNUSED,
                                  gpointer         user_data)
{
  OozeSniWatcher *watcher = user_data;
  g_autoptr (GError) error = NULL;

  watcher->session = connection;
  watcher->introspection = g_dbus_node_info_new_for_xml (ooze_sni_watcher_xml, &error);
  if (!watcher->introspection)
    {
      g_warning ("Ooze SNI: introspection parse failed: %s", error->message);
      return;
    }

  watcher->reg_id =
    g_dbus_connection_register_object (connection,
                                       OOZE_SNI_WATCHER_PATH,
                                       watcher->introspection->interfaces[0],
                                       &ooze_sni_watcher_vtable,
                                       watcher,
                                       NULL,
                                       &error);
  if (!watcher->reg_id)
    {
      g_warning ("Ooze SNI: failed to register watcher object: %s",
                 error->message);
      return;
    }

  ooze_sni_watcher_emit (watcher, "StatusNotifierHostRegistered", NULL);
  g_print ("Ooze SNI: StatusNotifierWatcher ready (host registered)\n");
}

static void
ooze_sni_watcher_on_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                                   const char      *name G_GNUC_UNUSED,
                                   gpointer         user_data G_GNUC_UNUSED)
{
}

static void
ooze_sni_watcher_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                               const char      *name G_GNUC_UNUSED,
                               gpointer         user_data)
{
  OozeSniWatcher *watcher = user_data;
  g_warning ("Ooze SNI: lost %s — another tray host may be running",
             OOZE_SNI_WATCHER_NAME);
  watcher->session = NULL;
}

OozeSniWatcher *
ooze_sni_watcher_new (OozeSniItemChangedFn changed_cb,
                      OozeSniItemRemovedFn removed_cb,
                      gpointer             user_data)
{
  OozeSniWatcher *watcher;

  watcher = g_new0 (OozeSniWatcher, 1);
  watcher->changed_cb = changed_cb;
  watcher->removed_cb = removed_cb;
  watcher->user_data = user_data;
  watcher->items = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, NULL);

  watcher->own_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    OOZE_SNI_WATCHER_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    ooze_sni_watcher_on_bus_acquired,
                    ooze_sni_watcher_on_name_acquired,
                    ooze_sni_watcher_on_name_lost,
                    watcher,
                    NULL);

  return watcher;
}

void
ooze_sni_watcher_free (OozeSniWatcher *watcher)
{
  GHashTableIter iter;
  gpointer value;

  if (!watcher)
    return;

  if (watcher->items)
    {
      g_hash_table_iter_init (&iter, watcher->items);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        ooze_sni_item_free (value);
      g_hash_table_destroy (watcher->items);
    }

  if (watcher->session && watcher->reg_id)
    g_dbus_connection_unregister_object (watcher->session, watcher->reg_id);
  if (watcher->own_name_id)
    g_bus_unown_name (watcher->own_name_id);
  if (watcher->introspection)
    g_dbus_node_info_unref (watcher->introspection);

  g_free (watcher);
}
