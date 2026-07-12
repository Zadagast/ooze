#include "my-global-menu.h"

#include <string.h>

typedef struct
{
  char     *action;
  GVariant *target;
} MyGlobalMenuAction;

struct _MyGlobalMenu
{
  MetaDisplay *display;

  char *bus_name;
  char *menubar_path;
  char *app_path;
  char *win_path;

  GDBusConnection *session;
  GMenuModel      *menubar;
  GActionGroup    *app_actions;
  GActionGroup    *win_actions;

  gulong focus_handler;
  gulong items_handler;

  MyGlobalMenuChangedFunc changed_cb;
  gpointer                changed_data;

  /* Live popup rows + action map for current open menu */
  MyAquaMenuEntry   *entries;
  gsize              n_entries;
  MyGlobalMenuAction *actions;
  gsize               n_actions;
};

static void my_global_menu_clear_proxy (MyGlobalMenu *menu);
static void my_global_menu_clear_entries (MyGlobalMenu *menu);
static void my_global_menu_emit_changed (MyGlobalMenu *menu);

static void
my_global_menu_emit_changed (MyGlobalMenu *menu)
{
  if (menu->changed_cb)
    menu->changed_cb (menu->changed_data);
}

static void
my_global_menu_clear_entries (MyGlobalMenu *menu)
{
  gsize i;

  if (menu->actions)
    {
      for (i = 0; i < menu->n_actions; i++)
        {
          g_free (menu->actions[i].action);
          g_clear_pointer (&menu->actions[i].target, g_variant_unref);
        }
      g_clear_pointer (&menu->actions, g_free);
      menu->n_actions = 0;
    }

  g_clear_pointer (&menu->entries, g_free);
  menu->n_entries = 0;
}

static void
on_menubar_items_changed (GMenuModel *model G_GNUC_UNUSED,
                          gint        position G_GNUC_UNUSED,
                          gint        removed G_GNUC_UNUSED,
                          gint        added G_GNUC_UNUSED,
                          MyGlobalMenu *menu)
{
  my_global_menu_emit_changed (menu);
}

static void
my_global_menu_clear_proxy (MyGlobalMenu *menu)
{
  my_global_menu_clear_entries (menu);

  if (menu->menubar && menu->items_handler)
    {
      g_signal_handler_disconnect (menu->menubar, menu->items_handler);
      menu->items_handler = 0;
    }

  g_clear_object (&menu->menubar);
  g_clear_object (&menu->app_actions);
  g_clear_object (&menu->win_actions);
  g_clear_pointer (&menu->bus_name, g_free);
  g_clear_pointer (&menu->menubar_path, g_free);
  g_clear_pointer (&menu->app_path, g_free);
  g_clear_pointer (&menu->win_path, g_free);
}

static gboolean
my_global_menu_paths_equal (MyGlobalMenu *menu,
                            const char   *bus,
                            const char   *menubar,
                            const char   *app,
                            const char   *win)
{
  return g_strcmp0 (menu->bus_name, bus) == 0 &&
         g_strcmp0 (menu->menubar_path, menubar) == 0 &&
         g_strcmp0 (menu->app_path, app) == 0 &&
         g_strcmp0 (menu->win_path, win) == 0;
}

static void
my_global_menu_bind_window (MyGlobalMenu *menu,
                            MetaWindow   *window)
{
  const char *bus;
  const char *menubar;
  const char *app_path;
  const char *win_path;

  if (!window)
    {
      if (menu->menubar)
        {
          my_global_menu_clear_proxy (menu);
          my_global_menu_emit_changed (menu);
        }
      return;
    }

  bus = meta_window_get_gtk_unique_bus_name (window);
  menubar = meta_window_get_gtk_menubar_object_path (window);
  app_path = meta_window_get_gtk_application_object_path (window);
  win_path = meta_window_get_gtk_window_object_path (window);

  if (!bus || !menubar)
    {
      if (menu->menubar)
        {
          my_global_menu_clear_proxy (menu);
          my_global_menu_emit_changed (menu);
        }
      return;
    }

  if (menu->menubar &&
      my_global_menu_paths_equal (menu, bus, menubar, app_path, win_path))
    return;

  my_global_menu_clear_proxy (menu);

  if (!menu->session)
    {
      g_autoptr (GError) error = NULL;
      menu->session = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
      if (!menu->session)
        {
          g_warning ("Ooze global menu: session bus unavailable: %s",
                     error ? error->message : "unknown");
          return;
        }
    }

  menu->bus_name = g_strdup (bus);
  menu->menubar_path = g_strdup (menubar);
  menu->app_path = g_strdup (app_path);
  menu->win_path = g_strdup (win_path);

  menu->menubar = G_MENU_MODEL (g_dbus_menu_model_get (menu->session,
                                                       menu->bus_name,
                                                       menu->menubar_path));
  if (menu->app_path)
    menu->app_actions = G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                                 menu->bus_name,
                                                                 menu->app_path));
  if (menu->win_path)
    menu->win_actions = G_ACTION_GROUP (g_dbus_action_group_get (menu->session,
                                                                 menu->bus_name,
                                                                 menu->win_path));

  menu->items_handler =
    g_signal_connect (menu->menubar, "items-changed",
                      G_CALLBACK (on_menubar_items_changed), menu);

  my_global_menu_emit_changed (menu);
}

static void
on_focus_window_changed (MetaDisplay  *display G_GNUC_UNUSED,
                         GParamSpec   *pspec G_GNUC_UNUSED,
                         MyGlobalMenu *menu)
{
  my_global_menu_sync_focus (menu);
}

MyGlobalMenu *
my_global_menu_new (MetaDisplay *display)
{
  MyGlobalMenu *menu;

  g_return_val_if_fail (META_IS_DISPLAY (display), NULL);

  menu = g_new0 (MyGlobalMenu, 1);
  menu->display = display;
  menu->focus_handler =
    g_signal_connect (display, "notify::focus-window",
                      G_CALLBACK (on_focus_window_changed), menu);

  my_global_menu_sync_focus (menu);
  return menu;
}

void
my_global_menu_free (MyGlobalMenu *menu)
{
  if (!menu)
    return;

  if (menu->display && menu->focus_handler)
    g_signal_handler_disconnect (menu->display, menu->focus_handler);

  my_global_menu_clear_proxy (menu);
  g_clear_object (&menu->session);
  g_free (menu);
}

void
my_global_menu_set_changed_callback (MyGlobalMenu           *menu,
                                     MyGlobalMenuChangedFunc callback,
                                     gpointer                user_data)
{
  g_return_if_fail (menu != NULL);
  menu->changed_cb = callback;
  menu->changed_data = user_data;
}

void
my_global_menu_sync_focus (MyGlobalMenu *menu)
{
  MetaWindow *focus;

  g_return_if_fail (menu != NULL);

  focus = meta_display_get_focus_window (menu->display);
  my_global_menu_bind_window (menu, focus);
}

gboolean
my_global_menu_has_app_menu (MyGlobalMenu *menu)
{
  return menu && menu->menubar != NULL &&
         g_menu_model_get_n_items (menu->menubar) > 0;
}

guint
my_global_menu_get_n_top (MyGlobalMenu *menu)
{
  gint n;

  if (!menu || !menu->menubar)
    return 0;

  n = g_menu_model_get_n_items (menu->menubar);
  if (n < 0)
    return 0;
  if ((guint) n > MY_GLOBAL_MENU_MAX_TOP)
    return MY_GLOBAL_MENU_MAX_TOP;
  return (guint) n;
}

const char *
my_global_menu_get_top_label (MyGlobalMenu *menu,
                              guint         index)
{
  g_autofree char *label = NULL;

  if (!menu || !menu->menubar || index >= my_global_menu_get_n_top (menu))
    return NULL;

  if (g_menu_model_get_item_attribute (menu->menubar, (int) index,
                                       G_MENU_ATTRIBUTE_LABEL, "s", &label))
    {
      /* Steal into a static buffer per-index would be racy; duplicate into
       * a single reusable slot on the menu object instead. */
      static char *cache[MY_GLOBAL_MENU_MAX_TOP];
      g_free (cache[index]);
      cache[index] = g_steal_pointer (&label);
      return cache[index];
    }

  return "Menu";
}

static int
my_global_menu_push_action (MyGlobalMenu *menu,
                            const char   *action,
                            GVariant     *target)
{
  MyGlobalMenuAction *slot;
  int id;

  menu->actions = g_renew (MyGlobalMenuAction, menu->actions, menu->n_actions + 1);
  slot = &menu->actions[menu->n_actions];
  slot->action = g_strdup (action);
  slot->target = target ? g_variant_ref_sink (target) : NULL;
  id = MY_MENU_APP_ACTION_BASE + (int) menu->n_actions;
  menu->n_actions++;
  return id;
}

gboolean
my_global_menu_fill_entries (MyGlobalMenu     *menu,
                             guint             top_index,
                             MyAquaMenuEntry **entries_out,
                             gsize            *n_entries_out)
{
  GMenuModel *section;
  gint n;
  gint i;
  g_autoptr (GPtrArray) rows = NULL;

  g_return_val_if_fail (menu != NULL, FALSE);
  g_return_val_if_fail (entries_out != NULL && n_entries_out != NULL, FALSE);

  my_global_menu_clear_entries (menu);
  *entries_out = NULL;
  *n_entries_out = 0;

  if (!menu->menubar || top_index >= my_global_menu_get_n_top (menu))
    return FALSE;

  section = g_menu_model_get_item_link (menu->menubar, (int) top_index,
                                        G_MENU_LINK_SUBMENU);
  if (!section)
    {
      /* Some exporters put actions directly on the top item. */
      g_autofree char *action = NULL;
      g_autoptr (GVariant) target = NULL;
      MyAquaMenuEntry *one;
      const char *label;

      label = my_global_menu_get_top_label (menu, top_index);
      if (!g_menu_model_get_item_attribute (menu->menubar, (int) top_index,
                                            G_MENU_ATTRIBUTE_ACTION, "s", &action))
        return FALSE;

      g_menu_model_get_item_attribute_value (menu->menubar, (int) top_index,
                                             G_MENU_ATTRIBUTE_TARGET, NULL);

      one = g_new0 (MyAquaMenuEntry, 1);
      one->label = label;
      one->action_id = my_global_menu_push_action (menu, action, NULL);
      one->sensitive = TRUE;
      menu->entries = one;
      menu->n_entries = 1;
      *entries_out = menu->entries;
      *n_entries_out = 1;
      return TRUE;
    }

  rows = g_ptr_array_new_with_free_func (g_free);
  n = g_menu_model_get_n_items (section);

  for (i = 0; i < n; i++)
    {
      MyAquaMenuEntry *row;
      g_autofree char *label = NULL;
      g_autofree char *action = NULL;
      g_autoptr (GVariant) target = NULL;
      GMenuModel *nested;

      nested = g_menu_model_get_item_link (section, i, G_MENU_LINK_SECTION);
      if (nested)
        {
          gint sn = g_menu_model_get_n_items (nested);
          gint si;

          if (rows->len > 0)
            {
              row = g_new0 (MyAquaMenuEntry, 1);
              row->label = NULL;
              row->action_id = 0;
              row->sensitive = FALSE;
              g_ptr_array_add (rows, row);
            }

          for (si = 0; si < sn; si++)
            {
              g_autofree char *slabel = NULL;
              g_autofree char *saction = NULL;
              g_autoptr (GVariant) starget = NULL;

              row = g_new0 (MyAquaMenuEntry, 1);
              if (!g_menu_model_get_item_attribute (nested, si,
                                                    G_MENU_ATTRIBUTE_LABEL, "s",
                                                    &slabel))
                {
                  /* Separator-like empty item */
                  row->label = NULL;
                  row->action_id = 0;
                  row->sensitive = FALSE;
                  g_ptr_array_add (rows, row);
                  continue;
                }

              row->label = g_intern_string (slabel);
              if (g_menu_model_get_item_attribute (nested, si,
                                                   G_MENU_ATTRIBUTE_ACTION, "s",
                                                   &saction))
                {
                  starget = g_menu_model_get_item_attribute_value (
                      nested, si, G_MENU_ATTRIBUTE_TARGET, NULL);
                  row->action_id = my_global_menu_push_action (menu, saction, starget);
                  row->sensitive = TRUE;
                }
              else
                {
                  row->action_id = 0;
                  row->sensitive = FALSE;
                }
              g_ptr_array_add (rows, row);
            }

          g_object_unref (nested);
          continue;
        }

      row = g_new0 (MyAquaMenuEntry, 1);

      if (!g_menu_model_get_item_attribute (section, i,
                                            G_MENU_ATTRIBUTE_LABEL, "s", &label))
        {
          row->label = NULL;
          row->action_id = 0;
          row->sensitive = FALSE;
          g_ptr_array_add (rows, row);
          continue;
        }

      row->label = g_intern_string (label);

      if (g_menu_model_get_item_attribute (section, i,
                                           G_MENU_ATTRIBUTE_ACTION, "s", &action))
        {
          target = g_menu_model_get_item_attribute_value (section, i,
                                                          G_MENU_ATTRIBUTE_TARGET,
                                                          NULL);
          row->action_id = my_global_menu_push_action (menu, action, target);
          row->sensitive = TRUE;
        }
      else
        {
          /* Nested submenu not expanded in this pass — show disabled. */
          row->action_id = 0;
          row->sensitive = FALSE;
        }

      g_ptr_array_add (rows, row);
    }

  g_object_unref (section);

  if (rows->len == 0)
    return FALSE;

  menu->n_entries = rows->len;
  menu->entries = g_new0 (MyAquaMenuEntry, menu->n_entries);
  for (i = 0; i < (gint) rows->len; i++)
    {
      MyAquaMenuEntry *src = rows->pdata[i];
      menu->entries[i] = *src;
      /* label is interned or NULL; clear src so free_func doesn't free label */
      src->label = NULL;
    }

  *entries_out = menu->entries;
  *n_entries_out = menu->n_entries;
  return TRUE;
}

static GActionGroup *
my_global_menu_resolve_group (MyGlobalMenu *menu,
                              const char   *action,
                              const char  **bare_name)
{
  const char *dot;

  *bare_name = action;

  if (!action)
    return NULL;

  dot = strchr (action, '.');
  if (dot && g_str_has_prefix (action, "win.") && menu->win_actions)
    {
      *bare_name = dot + 1;
      return menu->win_actions;
    }
  if (dot && g_str_has_prefix (action, "app.") && menu->app_actions)
    {
      *bare_name = dot + 1;
      return menu->app_actions;
    }

  if (menu->win_actions &&
      g_action_group_has_action (menu->win_actions, action))
    return menu->win_actions;
  if (menu->app_actions &&
      g_action_group_has_action (menu->app_actions, action))
    return menu->app_actions;

  return menu->win_actions ? menu->win_actions : menu->app_actions;
}

static gboolean
my_global_menu_try_activate (GActionGroup *group,
                             const char   *name,
                             GVariant     *target)
{
  if (!group || !name || !*name)
    return FALSE;

  /* GDBusActionGroup may not have DescribeAll yet; still send Activate. */
  g_action_group_activate_action (group, name, target);
  return TRUE;
}

void
my_global_menu_activate (MyGlobalMenu *menu,
                         int           action_id)
{
  int index;
  MyGlobalMenuAction *slot;
  GActionGroup *group;
  const char *bare;

  if (!menu || action_id < MY_MENU_APP_ACTION_BASE)
    return;

  index = action_id - MY_MENU_APP_ACTION_BASE;
  if (index < 0 || (gsize) index >= menu->n_actions)
    return;

  slot = &menu->actions[index];
  if (!slot->action)
    return;

  group = my_global_menu_resolve_group (menu, slot->action, &bare);
  if (group && bare && my_global_menu_try_activate (group, bare, slot->target))
    return;

  /* Fallbacks: full detailed name on either group. */
  if (menu->win_actions &&
      my_global_menu_try_activate (menu->win_actions, slot->action, slot->target))
    return;
  if (menu->app_actions &&
      my_global_menu_try_activate (menu->app_actions, slot->action, slot->target))
    return;

  g_warning ("Ooze global menu: could not activate '%s'", slot->action);
}
