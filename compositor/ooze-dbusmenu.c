#include "ooze-dbusmenu.h"

#include <string.h>

struct _OozeDbusmenu
{
  GDBusConnection *session; /* borrowed */
  char *bus_name;
  char *object_path;
};

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
  return menu;
}

void
ooze_dbusmenu_free (OozeDbusmenu *menu)
{
  if (!menu)
    return;
  g_free (menu->bus_name);
  g_free (menu->object_path);
  g_free (menu);
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

static void
ooze_dbusmenu_set_status_normal (OozeDbusmenu *menu)
{
  g_autoptr (GError) error = NULL;

  g_dbus_connection_call_sync (menu->session,
                               menu->bus_name,
                               menu->object_path,
                               "org.freedesktop.DBus.Properties",
                               "Set",
                               g_variant_new ("(ssv)",
                                              "com.canonical.dbusmenu",
                                              "Status",
                                              g_variant_new_string ("normal")),
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE,
                               500,
                               NULL,
                               &error);
}

static GVariant *
ooze_dbusmenu_get_layout_once (OozeDbusmenu *menu,
                             int         parent_id,
                             int         depth)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) props = NULL;
  GVariant *layout = NULL;
  guint32 revision = 0;
  static const char *const prop_names[] = {
    "label", "type", "enabled", "visible", "children-display", "toggle-type",
    "toggle-state", "shortcut", "icon-name", "icon-data", NULL
  };

  /* Empty property list = "all props" for some exporters; filtered list for others. */
  props = g_variant_ref_sink (g_variant_new_strv (NULL, 0));
  reply = g_dbus_connection_call_sync (menu->session,
                                       menu->bus_name,
                                       menu->object_path,
                                       "com.canonical.dbusmenu",
                                       "GetLayout",
                                       g_variant_new ("(ii@as)",
                                                      parent_id,
                                                      depth,
                                                      props),
                                       G_VARIANT_TYPE ("(u(ia{sv}av))"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       2500,
                                       NULL,
                                       &error);
  if (!reply)
    {
      g_clear_error (&error);
      props = g_variant_ref_sink (g_variant_new_strv (prop_names, -1));
      reply = g_dbus_connection_call_sync (menu->session,
                                           menu->bus_name,
                                           menu->object_path,
                                           "com.canonical.dbusmenu",
                                           "GetLayout",
                                           g_variant_new ("(ii@as)",
                                                          parent_id,
                                                          depth,
                                                          props),
                                           G_VARIANT_TYPE ("(u(ia{sv}av))"),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           2500,
                                           NULL,
                                           &error);
    }

  if (!reply)
    {
      g_warning ("Ooze dbusmenu: GetLayout(%d,%d) failed: %s",
                 parent_id, depth,
                 error ? error->message : "unknown");
      return NULL;
    }

  g_variant_get (reply, "(u@(ia{sv}av))", &revision, &layout);
  return layout;
}

static GVariant *
ooze_dbusmenu_get_layout (OozeDbusmenu *menu,
                        int         parent_id,
                        int         depth)
{
  gboolean need_update = FALSE;
  GVariant *layout;

  ooze_dbusmenu_set_status_normal (menu);

  /* Populate lazy exporters (Inkscape) before the first layout read. */
  need_update = ooze_dbusmenu_about_to_show (menu, parent_id);

  layout = ooze_dbusmenu_get_layout_once (menu, parent_id, depth);
  if (!layout && depth != -1)
    layout = ooze_dbusmenu_get_layout_once (menu, parent_id, -1);

  if (need_update)
    {
      g_clear_pointer (&layout, g_variant_unref);
      layout = ooze_dbusmenu_get_layout_once (menu, parent_id, depth);
      if (!layout && depth != -1)
        layout = ooze_dbusmenu_get_layout_once (menu, parent_id, -1);
    }

  return layout;
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

gboolean
ooze_dbusmenu_get_top_items (OozeDbusmenu      *menu,
                           OozeDbusmenuItem **items_out,
                           gsize           *n_out)
{
  g_autoptr (GVariant) layout = NULL;

  g_return_val_if_fail (menu != NULL, FALSE);

  /* Depth 1: root + immediate children (the menubar labels). */
  layout = ooze_dbusmenu_get_layout (menu, 0, 1);
  if (!layout)
    return FALSE;

  return ooze_dbusmenu_collect_children (layout, items_out, n_out);
}

gboolean
ooze_dbusmenu_get_children (OozeDbusmenu      *menu,
                          int              parent_id,
                          OozeDbusmenuItem **items_out,
                          gsize           *n_out)
{
  g_autoptr (GVariant) layout = NULL;

  g_return_val_if_fail (menu != NULL, FALSE);

  /* Depth 2 gets one submenu level; nested sections flatten via has_children. */
  layout = ooze_dbusmenu_get_layout (menu, parent_id, 2);
  if (!layout)
    return FALSE;

  if (!ooze_dbusmenu_collect_children (layout, items_out, n_out))
    {
      /* Some exporters only fill children after a second AboutToShow. */
      g_clear_pointer (&layout, g_variant_unref);
      layout = ooze_dbusmenu_get_layout (menu, parent_id, -1);
      if (!layout)
        return FALSE;
      return ooze_dbusmenu_collect_children (layout, items_out, n_out);
    }

  return TRUE;
}

gboolean
ooze_dbusmenu_about_to_show (OozeDbusmenu *menu,
                           int         id)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  gboolean need_update = FALSE;

  g_return_val_if_fail (menu != NULL, FALSE);

  reply = g_dbus_connection_call_sync (menu->session,
                                       menu->bus_name,
                                       menu->object_path,
                                       "com.canonical.dbusmenu",
                                       "AboutToShow",
                                       g_variant_new ("(i)", id),
                                       G_VARIANT_TYPE ("(b)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       1500,
                                       NULL,
                                       &error);
  if (!reply)
    return FALSE;

  g_variant_get (reply, "(b)", &need_update);
  return need_update;
}

gboolean
ooze_dbusmenu_opened (OozeDbusmenu *menu,
                    int         id)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  guint32 ts;

  g_return_val_if_fail (menu != NULL, FALSE);

  ooze_dbusmenu_set_status_normal (menu);
  ts = (guint32) (g_get_monotonic_time () / 1000);
  reply = g_dbus_connection_call_sync (menu->session,
                                       menu->bus_name,
                                       menu->object_path,
                                       "com.canonical.dbusmenu",
                                       "Event",
                                       g_variant_new ("(isvu)",
                                                      id,
                                                      "opened",
                                                      g_variant_new_int32 (0),
                                                      ts),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       800,
                                       NULL,
                                       &error);
  return reply != NULL;
}

gboolean
ooze_dbusmenu_click (OozeDbusmenu *menu,
                   int         id)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  guint32 ts;

  g_return_val_if_fail (menu != NULL, FALSE);

  ooze_dbusmenu_set_status_normal (menu);
  ooze_dbusmenu_about_to_show (menu, id);

  ts = (guint32) (g_get_monotonic_time () / 1000);
  reply = g_dbus_connection_call_sync (menu->session,
                                       menu->bus_name,
                                       menu->object_path,
                                       "com.canonical.dbusmenu",
                                       "Event",
                                       g_variant_new ("(isvu)",
                                                      id,
                                                      "clicked",
                                                      g_variant_new_int32 (0),
                                                      ts),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       1500,
                                       NULL,
                                       &error);
  if (!reply)
    {
      g_warning ("Ooze dbusmenu: Event(%d) failed: %s",
                 id, error ? error->message : "unknown");
      return FALSE;
    }
  return TRUE;
}
