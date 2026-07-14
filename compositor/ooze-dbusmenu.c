#include "ooze-dbusmenu.h"

#include <string.h>

#define OOZE_DBUSMENU_IFACE "com.canonical.dbusmenu"
#define OOZE_DBUSMENU_CALL_TIMEOUT_MS 2000

typedef struct
{
  OozeDbusmenuItem *items;
  gsize           n;
} OozeDbusmenuItemList;

typedef struct
{
  OozeDbusmenu *menu;
  int         parent_id;
  int         depth;
  gboolean    retried_props;
  gboolean    retried_depth;
} OozeDbusmenuFetch;

struct _OozeDbusmenu
{
  GDBusConnection *session; /* borrowed */
  char *bus_name;
  char *object_path;

  GCancellable *cancellable;
  guint layout_updated_id;
  guint items_updated_id;

  GHashTable *children; /* parent_id -> OozeDbusmenuItemList* */
  GHashTable *pending;  /* parent_id -> in-flight marker */

  OozeDbusmenuChangedFn changed_cb;
  gpointer            changed_data;
};

static void ooze_dbusmenu_fetch_layout (OozeDbusmenu *menu,
                                      int         parent_id,
                                      int         depth);

static void
ooze_dbusmenu_item_list_free (gpointer data)
{
  OozeDbusmenuItemList *list = data;

  if (!list)
    return;
  ooze_dbusmenu_items_free (list->items, list->n);
  g_free (list);
}

OozeDbusmenu *
ooze_dbusmenu_new (GDBusConnection *session,
                 const char      *bus_name,
                 const char      *object_path)
{
  OozeDbusmenu *menu;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (session), NULL);
  g_return_val_if_fail (bus_name && *bus_name, NULL);
  g_return_val_if_fail (object_path && *object_path, NULL);

  menu = g_new0 (OozeDbusmenu, 1);
  menu->session = session;
  menu->bus_name = g_strdup (bus_name);
  menu->object_path = g_strdup (object_path);
  menu->cancellable = g_cancellable_new ();
  menu->children = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, ooze_dbusmenu_item_list_free);
  menu->pending = g_hash_table_new (g_direct_hash, g_direct_equal);
  return menu;
}

void
ooze_dbusmenu_free (OozeDbusmenu *menu)
{
  if (!menu)
    return;

  g_cancellable_cancel (menu->cancellable);
  g_clear_object (&menu->cancellable);

  if (menu->layout_updated_id)
    g_dbus_connection_signal_unsubscribe (menu->session,
                                          menu->layout_updated_id);
  if (menu->items_updated_id)
    g_dbus_connection_signal_unsubscribe (menu->session,
                                          menu->items_updated_id);

  g_hash_table_destroy (menu->children);
  g_hash_table_destroy (menu->pending);
  g_free (menu->bus_name);
  g_free (menu->object_path);
  g_free (menu);
}

void
ooze_dbusmenu_set_changed_callback (OozeDbusmenu          *menu,
                                  OozeDbusmenuChangedFn  callback,
                                  gpointer             user_data)
{
  g_return_if_fail (menu != NULL);
  menu->changed_cb = callback;
  menu->changed_data = user_data;
}

void
ooze_dbusmenu_items_free (OozeDbusmenuItem *items,
                        gsize           n)
{
  gsize i;

  if (!items)
    return;
  for (i = 0; i < n; i++)
    g_free (items[i].label);
  g_free (items);
}

static char *
ooze_dbusmenu_clean_label (const char *raw)
{
  GString *out;
  const char *p;

  if (!raw)
    return g_strdup ("");

  out = g_string_new (NULL);
  p = raw;
  while (*p)
    {
      if (p[0] == '_' && p[1] != '\0')
        {
          if (p[1] == '_')
            {
              g_string_append_c (out, '_');
              p += 2;
            }
          else
            p++;
          continue;
        }
      g_string_append_unichar (out, g_utf8_get_char (p));
      p = g_utf8_next_char (p);
    }
  return g_string_free (out, FALSE);
}

static gboolean
ooze_dbusmenu_parse_node (GVariant       *node,
                        OozeDbusmenuItem *out)
{
  gint32 id = 0;
  g_autoptr (GVariant) props = NULL;
  g_autoptr (GVariant) children = NULL;
  const char *label = NULL;
  const char *type = NULL;
  gboolean enabled = TRUE;
  gboolean visible = TRUE;
  const char *children_display = NULL;

  memset (out, 0, sizeof (*out));
  out->enabled = TRUE;
  out->visible = TRUE;

  if (!node)
    return FALSE;

  if (g_variant_is_of_type (node, G_VARIANT_TYPE ("(ia{sv}av)")))
    g_variant_get (node, "(i@a{sv}@av)", &id, &props, &children);
  else if (g_variant_is_of_type (node, G_VARIANT_TYPE_VARIANT))
    {
      g_autoptr (GVariant) inner = g_variant_get_variant (node);

      return ooze_dbusmenu_parse_node (inner, out);
    }
  else
    return FALSE;

  out->id = id;
  if (props)
    {
      g_variant_lookup (props, "label", "&s", &label);
      g_variant_lookup (props, "type", "&s", &type);
      g_variant_lookup (props, "enabled", "b", &enabled);
      g_variant_lookup (props, "visible", "b", &visible);
      g_variant_lookup (props, "children-display", "&s", &children_display);
    }

  out->enabled = enabled;
  out->visible = visible;
  out->separator = (type && g_strcmp0 (type, "separator") == 0);
  out->has_children = (children_display &&
                       g_strcmp0 (children_display, "submenu") == 0) ||
                      (children && g_variant_n_children (children) > 0);
  out->label = ooze_dbusmenu_clean_label (label ? label : "");
  return TRUE;
}

static gboolean
ooze_dbusmenu_collect_children (GVariant        *parent_layout,
                              OozeDbusmenuItem **items_out,
                              gsize           *n_out)
{
  g_autoptr (GVariant) props = NULL;
  g_autoptr (GVariant) children = NULL;
  gint32 id = 0;
  GPtrArray *arr;
  gsize i;
  gsize n;

  *items_out = NULL;
  *n_out = 0;

  if (!parent_layout)
    return FALSE;

  g_variant_get (parent_layout, "(i@a{sv}@av)", &id, &props, &children);
  if (!children)
    return FALSE;

  arr = g_ptr_array_new_with_free_func (NULL);
  n = g_variant_n_children (children);
  for (i = 0; i < n; i++)
    {
      g_autoptr (GVariant) child = g_variant_get_child_value (children, i);
      OozeDbusmenuItem item;

      if (!ooze_dbusmenu_parse_node (child, &item))
        continue;
      if (!item.visible)
        {
          g_free (item.label);
          continue;
        }
      if (!item.separator && (!item.label || !*item.label) && !item.has_children)
        {
          g_free (item.label);
          continue;
        }

      {
        OozeDbusmenuItem *heap = g_memdup2 (&item, sizeof (item));

        g_ptr_array_add (arr, heap);
      }
    }

  if (arr->len == 0)
    {
      g_ptr_array_free (arr, TRUE);
      return FALSE;
    }

  *items_out = g_new0 (OozeDbusmenuItem, arr->len);
  *n_out = arr->len;
  for (i = 0; i < arr->len; i++)
    {
      OozeDbusmenuItem *src = arr->pdata[i];

      (*items_out)[i] = *src;
      g_free (src);
    }
  g_ptr_array_set_free_func (arr, NULL);
  g_ptr_array_free (arr, TRUE);
  return TRUE;
}

static void
ooze_dbusmenu_emit_changed (OozeDbusmenu *menu)
{
  if (menu->changed_cb)
    menu->changed_cb (menu->changed_data);
}

static void
ooze_dbusmenu_store_children (OozeDbusmenu     *menu,
                            int             parent_id,
                            OozeDbusmenuItem *items,
                            gsize           n)
{
  OozeDbusmenuItemList *list;

  list = g_new0 (OozeDbusmenuItemList, 1);
  list->items = items;
  list->n = n;
  g_hash_table_replace (menu->children, GINT_TO_POINTER (parent_id), list);
}

static void
ooze_dbusmenu_call (OozeDbusmenu       *menu,
                  const char         *method,
                  GVariant           *params,
                  const GVariantType *reply_type,
                  GAsyncReadyCallback callback,
                  gpointer            user_data)
{
  g_dbus_connection_call (menu->session,
                          menu->bus_name,
                          menu->object_path,
                          OOZE_DBUSMENU_IFACE,
                          method,
                          params,
                          reply_type,
                          G_DBUS_CALL_FLAGS_NONE,
                          OOZE_DBUSMENU_CALL_TIMEOUT_MS,
                          menu->cancellable,
                          callback,
                          user_data);
}

static void
ooze_dbusmenu_call_ignore_reply_cb (GObject      *source,
                                  GAsyncResult *res,
                                  gpointer      user_data G_GNUC_UNUSED)
{
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, NULL);
}

static void
ooze_dbusmenu_set_status_normal (OozeDbusmenu *menu)
{
  g_dbus_connection_call (menu->session,
                          menu->bus_name,
                          menu->object_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         OOZE_DBUSMENU_IFACE,
                                         "Status",
                                         g_variant_new_string ("normal")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          500,
                          menu->cancellable,
                          ooze_dbusmenu_call_ignore_reply_cb,
                          NULL);
}

static GVariant *
ooze_dbusmenu_layout_props (gboolean filtered)
{
  static const char *const prop_names[] = {
    "label", "type", "enabled", "visible", "children-display", "toggle-type",
    "toggle-state", "shortcut", "icon-name", "icon-data", NULL
  };

  /* Empty property list = "all props" for some exporters; filtered for others. */
  if (filtered)
    return g_variant_new_strv (prop_names, -1);
  return g_variant_new_strv (NULL, 0);
}

static void
ooze_dbusmenu_fetch_finish (OozeDbusmenuFetch *fetch,
                          GVariant         *layout)
{
  OozeDbusmenu *menu = fetch->menu;
  OozeDbusmenuItem *items = NULL;
  gsize n = 0;

  g_hash_table_remove (menu->pending, GINT_TO_POINTER (fetch->parent_id));

  if (layout && ooze_dbusmenu_collect_children (layout, &items, &n))
    {
      ooze_dbusmenu_store_children (menu, fetch->parent_id, items, n);
      ooze_dbusmenu_emit_changed (menu);
    }

  g_free (fetch);
}

static void
ooze_dbusmenu_get_layout_cb (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  OozeDbusmenuFetch *fetch = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_free (fetch);
      return;
    }

  if (!reply)
    {
      OozeDbusmenu *menu = fetch->menu;

      /* Some exporters reject the empty prop list; others reject depth. */
      if (!fetch->retried_props)
        {
          fetch->retried_props = TRUE;
          ooze_dbusmenu_call (menu, "GetLayout",
                            g_variant_new ("(ii@as)",
                                           fetch->parent_id,
                                           fetch->depth,
                                           ooze_dbusmenu_layout_props (TRUE)),
                            G_VARIANT_TYPE ("(u(ia{sv}av))"),
                            ooze_dbusmenu_get_layout_cb,
                            fetch);
          return;
        }
      if (!fetch->retried_depth && fetch->depth != -1)
        {
          fetch->retried_depth = TRUE;
          fetch->depth = -1;
          ooze_dbusmenu_call (menu, "GetLayout",
                            g_variant_new ("(ii@as)",
                                           fetch->parent_id,
                                           fetch->depth,
                                           ooze_dbusmenu_layout_props (TRUE)),
                            G_VARIANT_TYPE ("(u(ia{sv}av))"),
                            ooze_dbusmenu_get_layout_cb,
                            fetch);
          return;
        }

      g_warning ("Ooze dbusmenu: GetLayout(%d,%d) failed: %s",
                 fetch->parent_id, fetch->depth,
                 error ? error->message : "unknown");
      ooze_dbusmenu_fetch_finish (fetch, NULL);
      return;
    }

  {
    g_autoptr (GVariant) layout = NULL;
    guint32 revision = 0;

    g_variant_get (reply, "(u@(ia{sv}av))", &revision, &layout);
    ooze_dbusmenu_fetch_finish (fetch, layout);
  }
}

static void
ooze_dbusmenu_about_to_show_cb (GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  OozeDbusmenuFetch *fetch = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_free (fetch);
      return;
    }

  /* Whether AboutToShow succeeded or not, read the layout now. */
  ooze_dbusmenu_call (fetch->menu, "GetLayout",
                    g_variant_new ("(ii@as)",
                                   fetch->parent_id,
                                   fetch->depth,
                                   ooze_dbusmenu_layout_props (FALSE)),
                    G_VARIANT_TYPE ("(u(ia{sv}av))"),
                    ooze_dbusmenu_get_layout_cb,
                    fetch);
}

static void
ooze_dbusmenu_fetch_layout (OozeDbusmenu *menu,
                          int         parent_id,
                          int         depth)
{
  OozeDbusmenuFetch *fetch;

  if (g_hash_table_contains (menu->pending, GINT_TO_POINTER (parent_id)))
    return;

  g_hash_table_add (menu->pending, GINT_TO_POINTER (parent_id));

  fetch = g_new0 (OozeDbusmenuFetch, 1);
  fetch->menu = menu;
  fetch->parent_id = parent_id;
  fetch->depth = depth;

  /* Populate lazy exporters (Inkscape) before the layout read. */
  ooze_dbusmenu_call (menu, "AboutToShow",
                    g_variant_new ("(i)", parent_id),
                    G_VARIANT_TYPE ("(b)"),
                    ooze_dbusmenu_about_to_show_cb,
                    fetch);
}

static void
on_dbusmenu_layout_updated (GDBusConnection *connection G_GNUC_UNUSED,
                            const char      *sender G_GNUC_UNUSED,
                            const char      *path G_GNUC_UNUSED,
                            const char      *iface G_GNUC_UNUSED,
                            const char      *signal G_GNUC_UNUSED,
                            GVariant        *params,
                            gpointer         user_data)
{
  OozeDbusmenu *menu = user_data;
  guint32 revision = 0;
  gint32 parent = 0;

  if (params && g_variant_is_of_type (params, G_VARIANT_TYPE ("(ui)")))
    g_variant_get (params, "(ui)", &revision, &parent);

  /* Layouts changed under us — drop the cache and refetch lazily. */
  g_hash_table_remove_all (menu->children);
  ooze_dbusmenu_fetch_layout (menu, 0, 1);
}

static void
on_dbusmenu_items_updated (GDBusConnection *connection G_GNUC_UNUSED,
                           const char      *sender G_GNUC_UNUSED,
                           const char      *path G_GNUC_UNUSED,
                           const char      *iface G_GNUC_UNUSED,
                           const char      *signal G_GNUC_UNUSED,
                           GVariant        *params G_GNUC_UNUSED,
                           gpointer         user_data)
{
  OozeDbusmenu *menu = user_data;

  g_hash_table_remove_all (menu->children);
  ooze_dbusmenu_fetch_layout (menu, 0, 1);
}

void
ooze_dbusmenu_start (OozeDbusmenu *menu)
{
  g_return_if_fail (menu != NULL);

  if (menu->layout_updated_id == 0)
    menu->layout_updated_id =
      g_dbus_connection_signal_subscribe (menu->session,
                                          menu->bus_name,
                                          OOZE_DBUSMENU_IFACE,
                                          "LayoutUpdated",
                                          menu->object_path,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          on_dbusmenu_layout_updated,
                                          menu,
                                          NULL);
  if (menu->items_updated_id == 0)
    menu->items_updated_id =
      g_dbus_connection_signal_subscribe (menu->session,
                                          menu->bus_name,
                                          OOZE_DBUSMENU_IFACE,
                                          "ItemsPropertiesUpdated",
                                          menu->object_path,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          on_dbusmenu_items_updated,
                                          menu,
                                          NULL);

  ooze_dbusmenu_set_status_normal (menu);
  ooze_dbusmenu_fetch_layout (menu, 0, 1);
}

static gboolean
ooze_dbusmenu_copy_cached (OozeDbusmenu      *menu,
                         int              parent_id,
                         OozeDbusmenuItem **items_out,
                         gsize           *n_out)
{
  OozeDbusmenuItemList *list;
  gsize i;

  list = g_hash_table_lookup (menu->children, GINT_TO_POINTER (parent_id));
  if (!list || list->n == 0)
    return FALSE;

  *items_out = g_new0 (OozeDbusmenuItem, list->n);
  *n_out = list->n;
  for (i = 0; i < list->n; i++)
    {
      (*items_out)[i] = list->items[i];
      (*items_out)[i].label = g_strdup (list->items[i].label);
    }
  return TRUE;
}

gboolean
ooze_dbusmenu_get_top_items (OozeDbusmenu      *menu,
                           OozeDbusmenuItem **items_out,
                           gsize           *n_out)
{
  g_return_val_if_fail (menu != NULL, FALSE);

  *items_out = NULL;
  *n_out = 0;

  if (ooze_dbusmenu_copy_cached (menu, 0, items_out, n_out))
    return TRUE;

  /* Depth 1: root + immediate children (the menubar labels). */
  ooze_dbusmenu_fetch_layout (menu, 0, 1);
  return FALSE;
}

gboolean
ooze_dbusmenu_get_children (OozeDbusmenu      *menu,
                          int              parent_id,
                          OozeDbusmenuItem **items_out,
                          gsize           *n_out)
{
  g_return_val_if_fail (menu != NULL, FALSE);

  *items_out = NULL;
  *n_out = 0;

  if (ooze_dbusmenu_copy_cached (menu, parent_id, items_out, n_out))
    return TRUE;

  /* Depth 2 gets one submenu level; nested sections flatten via has_children. */
  ooze_dbusmenu_fetch_layout (menu, parent_id, 2);
  return FALSE;
}

static void
ooze_dbusmenu_send_event (OozeDbusmenu *menu,
                        int         id,
                        const char *event)
{
  guint32 ts;

  ts = (guint32) (g_get_monotonic_time () / 1000);
  ooze_dbusmenu_call (menu, "Event",
                    g_variant_new ("(isvu)",
                                   id,
                                   event,
                                   g_variant_new_int32 (0),
                                   ts),
                    NULL,
                    ooze_dbusmenu_call_ignore_reply_cb,
                    NULL);
}

void
ooze_dbusmenu_opened (OozeDbusmenu *menu,
                    int         id)
{
  g_return_if_fail (menu != NULL);

  ooze_dbusmenu_send_event (menu, id, "opened");
}

void
ooze_dbusmenu_click (OozeDbusmenu *menu,
                   int         id)
{
  g_return_if_fail (menu != NULL);

  ooze_dbusmenu_call (menu, "AboutToShow",
                    g_variant_new ("(i)", id),
                    G_VARIANT_TYPE ("(b)"),
                    ooze_dbusmenu_call_ignore_reply_cb,
                    NULL);
  ooze_dbusmenu_send_event (menu, id, "clicked");
}
