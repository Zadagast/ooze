#include "ooze-notifications.h"

#include "ooze-aqua-draw.h"
#include "ooze-plugin-priv.h"
#include "ooze-theme.h"
#include "../common/ooze-font.h"
#include "../shared/ooze-icon-lookup.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>

#include <clutter/clutter.h>
#include <gio/gio.h>
#include <string.h>

#define OOZE_NOTIFICATIONS_BUS_NAME "org.freedesktop.Notifications"
#define OOZE_NOTIFICATIONS_OBJECT_PATH "/org/freedesktop/Notifications"
#define OOZE_NOTIFICATIONS_INTERFACE "org.freedesktop.Notifications"
#define OOZE_NOTIFICATION_DEFAULT_TIMEOUT 5000
#define OOZE_NOTIFICATION_WIDTH 360.0f
#define OOZE_NOTIFICATION_MARGIN 18.0f
#define OOZE_NOTIFICATION_GAP 10.0f
#define OOZE_NOTIFICATION_ICON_SIZE 36.0f

typedef struct _OozeNotification OozeNotification;

typedef struct
{
  OozeNotification *notification;
  ClutterActor     *actor;
  gulong            handler_id;
  char             *action_id;
} OozeNotificationAction;

struct _OozeNotification
{
  OozeNotifications *server;
  guint32            id;
  char              *app_name;
  char              *app_icon;
  char              *summary;
  char              *body;
  char              *default_action;
  char             **actions;
  gboolean           critical;
  guint               timeout_id;
  ClutterActor       *card;
  ClutterActor       *close_button;
  gulong              card_handler_id;
  gulong              close_handler_id;
  GPtrArray          *action_buttons; /* OozeNotificationAction* */
};

struct _OozeNotifications
{
  OozePlugin       *plugin; /* borrowed; owned by the plugin lifecycle */
  GDBusNodeInfo    *introspection;
  GDBusConnection  *connection;
  guint             registration_id;
  guint             own_name_id;
  guint32           next_id;
  GHashTable       *notifications; /* guint32 -> OozeNotification* */
  GPtrArray        *order;         /* OozeNotification*; newest first */
  ClutterActor     *layer;
};

static const char notification_introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.Notifications'>"
  "    <method name='Notify'>"
  "      <arg type='s' name='app_name' direction='in'/>"
  "      <arg type='s' name='app_icon' direction='in'/>"
  "      <arg type='u' name='replaces_id' direction='in'/>"
  "      <arg type='s' name='summary' direction='in'/>"
  "      <arg type='s' name='body' direction='in'/>"
  "      <arg type='as' name='actions' direction='in'/>"
  "      <arg type='a{sv}' name='hints' direction='in'/>"
  "      <arg type='i' name='expire_timeout' direction='in'/>"
  "      <arg type='u' name='id' direction='out'/>"
  "    </method>"
  "    <method name='CloseNotification'>"
  "      <arg type='u' name='id' direction='in'/>"
  "    </method>"
  "    <method name='GetCapabilities'>"
  "      <arg type='as' name='capabilities' direction='out'/>"
  "    </method>"
  "    <method name='GetServerInformation'>"
  "      <arg type='s' name='name' direction='out'/>"
  "      <arg type='s' name='vendor' direction='out'/>"
  "      <arg type='s' name='version' direction='out'/>"
  "      <arg type='s' name='spec_version' direction='out'/>"
  "    </method>"
  "    <signal name='NotificationClosed'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='u' name='reason'/>"
  "    </signal>"
  "    <signal name='ActionInvoked'>"
  "      <arg type='u' name='id'/>"
  "      <arg type='s' name='action_key'/>"
  "    </signal>"
  "  </interface>"
  "</node>";

void ooze_notifications_reflow (OozeNotifications *server);

static gboolean
ooze_notification_is_live (OozeNotification *notification)
{
  return notification && notification->server &&
         g_hash_table_lookup (notification->server->notifications,
                              GUINT_TO_POINTER (notification->id)) ==
           notification;
}

static void
ooze_notifications_emit_closed (OozeNotifications *server,
                                 guint32            id,
                                 guint32            reason)
{
  if (!server->connection)
    return;

  g_dbus_connection_emit_signal (server->connection,
                                 NULL,
                                 OOZE_NOTIFICATIONS_OBJECT_PATH,
                                 OOZE_NOTIFICATIONS_INTERFACE,
                                 "NotificationClosed",
                                 g_variant_new ("(uu)", id, reason),
                                 NULL);
}

static void
ooze_notifications_emit_action (OozeNotifications *server,
                                 guint32            id,
                                 const char        *action_id)
{
  if (!server->connection)
    return;

  g_dbus_connection_emit_signal (server->connection,
                                 NULL,
                                 OOZE_NOTIFICATIONS_OBJECT_PATH,
                                 OOZE_NOTIFICATIONS_INTERFACE,
                                 "ActionInvoked",
                                 g_variant_new ("(us)", id, action_id),
                                 NULL);
}

static void
ooze_notification_action_free (gpointer data)
{
  OozeNotificationAction *action = data;

  if (!action)
    return;
  if (action->actor && action->handler_id)
    g_signal_handler_disconnect (action->actor, action->handler_id);
  g_free (action->action_id);
  g_free (action);
}

static void
ooze_notification_destroy_card (OozeNotification *notification)
{
  guint i;

  if (!notification)
    return;

  if (notification->timeout_id)
    {
      g_source_remove (notification->timeout_id);
      notification->timeout_id = 0;
    }

  if (notification->card && notification->card_handler_id)
    {
      g_signal_handler_disconnect (notification->card,
                                   notification->card_handler_id);
      notification->card_handler_id = 0;
    }
  if (notification->close_button && notification->close_handler_id)
    {
      g_signal_handler_disconnect (notification->close_button,
                                   notification->close_handler_id);
      notification->close_handler_id = 0;
    }

  if (notification->action_buttons)
    {
      for (i = 0; i < notification->action_buttons->len; i++)
        {
          OozeNotificationAction *action =
            notification->action_buttons->pdata[i];
          if (action->actor && action->handler_id)
            {
              g_signal_handler_disconnect (action->actor,
                                           action->handler_id);
              action->handler_id = 0;
            }
        }
      g_ptr_array_set_size (notification->action_buttons, 0);
    }

  if (notification->card)
    clutter_actor_destroy (notification->card);
  notification->card = NULL;
  notification->close_button = NULL;
}

static void
ooze_notification_free (gpointer data)
{
  OozeNotification *notification = data;

  if (!notification)
    return;

  ooze_notification_destroy_card (notification);
  g_clear_pointer (&notification->action_buttons, g_ptr_array_unref);
  g_free (notification->app_name);
  g_free (notification->app_icon);
  g_free (notification->summary);
  g_free (notification->body);
  g_free (notification->default_action);
  g_strfreev (notification->actions);
  g_free (notification);
}

static void
ooze_notifications_remove_from_order (OozeNotifications *server,
                                       OozeNotification *notification)
{
  guint i;

  for (i = 0; i < server->order->len; i++)
    {
      if (server->order->pdata[i] == notification)
        {
          g_ptr_array_remove_index (server->order, i);
          return;
        }
    }
}

static void
ooze_notification_close (OozeNotification *notification,
                         guint32            reason)
{
  OozeNotifications *server;
  guint32 id;

  if (!ooze_notification_is_live (notification))
    return;

  server = notification->server;
  id = notification->id;
  ooze_notifications_remove_from_order (server, notification);
  g_hash_table_remove (server->notifications, GUINT_TO_POINTER (id));
  ooze_notifications_emit_closed (server, id, reason);
  ooze_notifications_reflow (server);
}

static gboolean
ooze_notification_expire_cb (gpointer user_data)
{
  OozeNotification *notification = user_data;

  notification->timeout_id = 0;
  ooze_notification_close (notification, 1);
  return G_SOURCE_REMOVE;
}

static gboolean
ooze_notification_close_button_cb (ClutterActor *actor G_GNUC_UNUSED,
                                   ClutterEvent *event G_GNUC_UNUSED,
                                   gpointer      user_data)
{
  ooze_notification_close (user_data, 2);
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_notification_default_action_cb (ClutterActor *actor G_GNUC_UNUSED,
                                     ClutterEvent *event G_GNUC_UNUSED,
                                     gpointer      user_data)
{
  OozeNotification *notification = user_data;

  if (!ooze_notification_is_live (notification) ||
      !notification->default_action)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_notifications_emit_action (notification->server,
                                  notification->id,
                                  notification->default_action);
  ooze_notification_close (notification, 2);
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_notification_action_button_cb (ClutterActor *actor G_GNUC_UNUSED,
                                    ClutterEvent *event G_GNUC_UNUSED,
                                    gpointer      user_data)
{
  OozeNotificationAction *action = user_data;
  OozeNotification *notification = action->notification;

  if (!ooze_notification_is_live (notification))
    return CLUTTER_EVENT_STOP;

  ooze_notifications_emit_action (notification->server,
                                  notification->id,
                                  action->action_id);
  ooze_notification_close (notification, 2);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
ooze_notification_text (ClutterActor *ref_actor,
                         const char   *font,
                         const char   *text,
                         gfloat        r,
                         gfloat        g,
                         gfloat        b,
                         int           max_width,
                         int          *width,
                         int          *height)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;
  int content_width = 1;
  int content_height = 1;

  actor = clutter_actor_new ();
  content = ooze_aqua_text_content_ellipsize (ref_actor,
                                              font,
                                              text ? text : "",
                                              r, g, b,
                                              max_width,
                                              &content_width,
                                              &content_height);
  if (content)
    ooze_aqua_actor_set_content (actor, g_steal_pointer (&content),
                                 content_width, content_height);
  else
    clutter_actor_set_size (actor, 1.0f, 1.0f);

  if (width)
    *width = content_width;
  if (height)
    *height = content_height;
  return actor;
}

static ClutterActor *
ooze_notification_make_button (OozeNotification *notification,
                               const char        *label,
                               OozeNotificationAction *action,
                               int                width)
{
  ClutterActor *button;
  ClutterActor *text;
  CoglColor color;
  int text_width;
  int text_height;

  button = clutter_actor_new ();
  clutter_actor_set_reactive (button, TRUE);
  clutter_actor_set_size (button, width, 24.0f);
  cogl_color_init_from_4f (&color, 0.04f, 0.11f, 0.16f, 0.28f);
  clutter_actor_set_background_color (button, &color);

  text = ooze_notification_text (notification->card,
                                 OOZE_UI_FONT,
                                 label,
                                 0.95f, 0.97f, 1.0f,
                                 width - 12,
                                 &text_width,
                                 &text_height);
  clutter_actor_add_child (button, text);
  clutter_actor_set_position (text,
                              (width - text_width) / 2.0f,
                              (24.0f - text_height) / 2.0f);

  if (action)
    {
      action->actor = button;
      action->handler_id =
        g_signal_connect (button, "button-press-event",
                          G_CALLBACK (ooze_notification_action_button_cb),
                          action);
    }
  return button;
}

static void
ooze_notification_build_card (OozeNotification *notification)
{
  OozeNotifications *server = notification->server;
  const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);
  ClutterActor *card;
  ClutterActor *icon = NULL;
  ClutterActor *summary;
  ClutterActor *body;
  ClutterActor *close;
  ClutterActor *actions_box;
  int summary_width;
  int summary_height;
  int body_width;
  int body_height;
  int width = (int) OOZE_NOTIFICATION_WIDTH;
  int y = 14;
  int text_x = 60;
  guint action_index = 0;
  guint i;

  card = clutter_actor_new ();
  clutter_actor_set_size (card, OOZE_NOTIFICATION_WIDTH, 1.0f);

  {
    g_autoptr (ClutterContent) background =
      ooze_aqua_squircle_panel_content (card, width, 1, FALSE);
    if (background)
      clutter_actor_set_content (card, g_steal_pointer (&background));
  }

  if (notification->app_icon && notification->app_icon[0])
    {
      g_autoptr (GdkPixbuf) pixbuf =
        ooze_icon_lookup_load (notification->app_icon,
                               (int) OOZE_NOTIFICATION_ICON_SIZE);
      if (pixbuf)
        {
          g_autoptr (ClutterContent) icon_content =
            ooze_aqua_content_from_pixbuf (card, pixbuf);
          icon = clutter_actor_new ();
          clutter_actor_set_size (icon,
                                  OOZE_NOTIFICATION_ICON_SIZE,
                                  OOZE_NOTIFICATION_ICON_SIZE);
          if (icon_content)
            clutter_actor_set_content (icon, g_steal_pointer (&icon_content));
          clutter_actor_add_child (card, icon);
          clutter_actor_set_position (icon, 14.0f, 16.0f);
        }
    }

  summary = ooze_notification_text (
    card,
    OOZE_UI_FONT_EMPHASIS,
    notification->summary[0] ? notification->summary : notification->app_name,
    palette->title_text_r, palette->title_text_g, palette->title_text_b,
    width - text_x - 42,
    &summary_width,
    &summary_height);
  clutter_actor_add_child (card, summary);
  clutter_actor_set_position (summary, text_x, y);

  y += summary_height + 4;
  body = ooze_notification_text (card,
                                 OOZE_UI_FONT,
                                 notification->body,
                                 palette->menu_text_r,
                                 palette->menu_text_g,
                                 palette->menu_text_b,
                                 width - text_x - 24,
                                 &body_width,
                                 &body_height);
  clutter_actor_add_child (card, body);
  clutter_actor_set_position (body, text_x, y);
  y += body_height + 12;

  close = ooze_notification_text (card, OOZE_UI_FONT_EMPHASIS, "×",
                                  palette->menu_text_r,
                                  palette->menu_text_g,
                                  palette->menu_text_b,
                                  24, NULL, NULL);
  clutter_actor_set_reactive (close, TRUE);
  clutter_actor_add_child (card, close);
  clutter_actor_set_position (close, width - 30.0f, 10.0f);

  actions_box = clutter_actor_new ();
  clutter_actor_set_layout_manager (actions_box, NULL);
  clutter_actor_add_child (card, actions_box);

  if (notification->actions)
    {
      for (i = 0; notification->actions[i] != NULL; i += 2)
        {
          const char *action_id = notification->actions[i];
          const char *label = notification->actions[i + 1];
          OozeNotificationAction *action;
          ClutterActor *button;
          gfloat button_x;

          if (!label)
            break;
          if (g_strcmp0 (action_id, "default") == 0)
            continue;

          action = g_new0 (OozeNotificationAction, 1);
          action->notification = notification;
          action->action_id = g_strdup (action_id);
          g_ptr_array_add (notification->action_buttons, action);
          button = ooze_notification_make_button (notification, label, action,
                                                   86);
          button_x = (gfloat) (action_index++ * 92);
          clutter_actor_add_child (actions_box, button);
          clutter_actor_set_position (button, button_x, 0.0f);
        }
    }

  if (notification->action_buttons->len > 0)
    {
      clutter_actor_set_size (actions_box,
                              notification->action_buttons->len * 92.0f,
                              24.0f);
      clutter_actor_set_position (actions_box, text_x, y);
      y += 34;
    }

  y = MAX (y, 76);
  clutter_actor_set_size (card, width, y);
  {
    g_autoptr (ClutterContent) background =
      ooze_aqua_squircle_panel_content (card, width, y, FALSE);
    if (background)
      clutter_actor_set_content (card, g_steal_pointer (&background));
  }

  if (notification->default_action)
    {
      clutter_actor_set_reactive (card, TRUE);
      notification->card_handler_id =
        g_signal_connect (card, "button-press-event",
                          G_CALLBACK (ooze_notification_default_action_cb),
                          notification);
    }

  notification->close_button = close;
  notification->close_handler_id =
    g_signal_connect (close, "button-press-event",
                      G_CALLBACK (ooze_notification_close_button_cb),
                      notification);
  notification->card = card;
  clutter_actor_add_child (server->layer, card);
}

void
ooze_notifications_reflow (OozeNotifications *server)
{
  MetaDisplay *display;
  MtkRectangle rect;
  int primary;
  int n_monitors;
  guint i;
  gfloat y;

  if (!server || !server->layer)
    return;

  display = meta_plugin_get_display (META_PLUGIN (server->plugin));
  if (!display)
    return;

  n_monitors = meta_display_get_n_monitors (display);
  if (n_monitors < 1)
    {
      meta_display_get_size (display, &rect.width, &rect.height);
      rect.x = 0;
      rect.y = 0;
    }
  else
    {
      primary = meta_display_get_primary_monitor (display);
      if (primary < 0 || primary >= n_monitors)
        primary = 0;
      meta_display_get_monitor_geometry (display, primary, &rect);
    }
  y = rect.y + OOZE_NOTIFICATION_MARGIN;

  for (i = 0; i < server->order->len; i++)
    {
      OozeNotification *notification = server->order->pdata[i];
      gfloat width;
      gfloat height;

      if (!notification->card)
        continue;
      width = clutter_actor_get_width (notification->card);
      height = clutter_actor_get_height (notification->card);
      clutter_actor_set_position (notification->card,
                                  rect.x + rect.width -
                                    width - OOZE_NOTIFICATION_MARGIN,
                                  y);
      y += height + OOZE_NOTIFICATION_GAP;
    }
}

static void
ooze_notification_schedule_expiry (OozeNotification *notification,
                                    gint32            expire_timeout)
{
  if (notification->critical || expire_timeout == 0)
    return;

  if (expire_timeout < 0)
    expire_timeout = OOZE_NOTIFICATION_DEFAULT_TIMEOUT;

  notification->timeout_id =
    g_timeout_add ((guint) expire_timeout,
                   ooze_notification_expire_cb,
                   notification);
}

static void
ooze_notification_update (OozeNotification *notification,
                           const char        *app_name,
                           const char        *app_icon,
                           const char        *summary,
                           const char        *body,
                           char             **actions,
                           gboolean           critical,
                           gint32             expire_timeout)
{
  ooze_notification_destroy_card (notification);
  g_free (notification->app_name);
  g_free (notification->app_icon);
  g_free (notification->summary);
  g_free (notification->body);
  g_free (notification->default_action);
  g_strfreev (notification->actions);

  notification->app_name = g_strdup (app_name);
  notification->app_icon = g_strdup (app_icon);
  notification->summary = g_strdup (summary);
  notification->body = g_strdup (body);
  notification->actions = g_steal_pointer (&actions);
  notification->critical = critical;

  if (notification->actions)
    {
      guint i;
      for (i = 0; notification->actions[i] &&
                    notification->actions[i + 1]; i += 2)
        {
          if (g_strcmp0 (notification->actions[i], "default") == 0)
            {
              notification->default_action =
                g_strdup (notification->actions[i]);
              break;
            }
        }
    }

  ooze_notification_build_card (notification);
  ooze_notification_schedule_expiry (notification, expire_timeout);
  ooze_notifications_reflow (notification->server);
}

static guint32
ooze_notifications_next_id (OozeNotifications *server)
{
  guint32 id;

  do
    {
      id = server->next_id++;
      if (id == 0)
        id = server->next_id++;
    }
  while (g_hash_table_contains (server->notifications,
                                GUINT_TO_POINTER (id)));

  return id;
}

static void
ooze_notifications_handle_notify (OozeNotifications *server,
                                   GDBusMethodInvocation *invocation,
                                   GVariant            *parameters)
{
  const char *app_name;
  const char *app_icon;
  guint32 replaces_id;
  const char *summary;
  const char *body;
  char **actions = NULL;
  GVariant *hints = NULL;
  gint32 expire_timeout;
  guint8 urgency = 1;
  gboolean critical;
  OozeNotification *notification;

  g_variant_get (parameters, "(&s&su&s&s^as@a{sv}i)",
                 &app_name, &app_icon, &replaces_id, &summary, &body,
                 &actions, &hints, &expire_timeout);
  g_variant_lookup (hints, "urgency", "y", &urgency);
  critical = urgency == 2;

  if (replaces_id != 0)
    notification = g_hash_table_lookup (server->notifications,
                                        GUINT_TO_POINTER (replaces_id));
  else
    notification = NULL;

  if (!notification)
    {
      notification = g_new0 (OozeNotification, 1);
      notification->server = server;
      notification->id = ooze_notifications_next_id (server);
      notification->action_buttons =
        g_ptr_array_new_with_free_func (ooze_notification_action_free);
      g_hash_table_insert (server->notifications,
                           GUINT_TO_POINTER (notification->id),
                           notification);
      g_ptr_array_insert (server->order, 0, notification);
    }

  ooze_notification_update (notification, app_name, app_icon, summary, body,
                            actions, critical, expire_timeout);
  g_variant_unref (hints);
  g_dbus_method_invocation_return_value (
    invocation, g_variant_new ("(u)", notification->id));
}

static void
ooze_notifications_handle_close (OozeNotifications       *server,
                                  GDBusMethodInvocation   *invocation,
                                  GVariant                *parameters)
{
  guint32 id;
  OozeNotification *notification;

  g_variant_get (parameters, "(u)", &id);
  notification = g_hash_table_lookup (server->notifications,
                                      GUINT_TO_POINTER (id));
  if (notification)
    ooze_notification_close (notification, 3);
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
ooze_notifications_method_call (GDBusConnection       *connection G_GNUC_UNUSED,
                                const char             *sender G_GNUC_UNUSED,
                                const char             *object_path G_GNUC_UNUSED,
                                const char             *interface_name G_GNUC_UNUSED,
                                const char             *method_name,
                                GVariant               *parameters,
                                GDBusMethodInvocation   *invocation,
                                gpointer                user_data)
{
  OozeNotifications *server = user_data;

  if (g_strcmp0 (method_name, "Notify") == 0)
    {
      ooze_notifications_handle_notify (server, invocation, parameters);
      return;
    }
  if (g_strcmp0 (method_name, "CloseNotification") == 0)
    {
      ooze_notifications_handle_close (server, invocation, parameters);
      return;
    }
  if (g_strcmp0 (method_name, "GetCapabilities") == 0)
    {
      const char *capabilities[] = {
        "body", "actions", "icon-static", NULL
      };
      g_dbus_method_invocation_return_value (
        invocation, g_variant_new ("(^as)", capabilities));
      return;
    }
  if (g_strcmp0 (method_name, "GetServerInformation") == 0)
    {
      g_dbus_method_invocation_return_value (
        invocation, g_variant_new ("(ssss)", "Ooze", "Zadagast",
                                   OOZE_VERSION, "1.2"));
      return;
    }

  g_dbus_method_invocation_return_error (invocation,
                                         G_IO_ERROR,
                                         G_IO_ERROR_NOT_SUPPORTED,
                                         "Unknown notification method %s",
                                         method_name);
}

static const GDBusInterfaceVTable notification_vtable = {
  ooze_notifications_method_call,
  NULL,
  NULL,
  { NULL },
};

static void
ooze_notifications_bus_acquired (GDBusConnection *connection,
                                 const char      *name G_GNUC_UNUSED,
                                 gpointer         user_data)
{
  OozeNotifications *server = user_data;
  g_autoptr (GError) error = NULL;

  server->connection = g_object_ref (connection);
  server->registration_id =
    g_dbus_connection_register_object (connection,
                                       OOZE_NOTIFICATIONS_OBJECT_PATH,
                                       server->introspection->interfaces[0],
                                       &notification_vtable,
                                       server,
                                       NULL,
                                       &error);
  if (!server->registration_id)
    g_warning ("Ooze notifications: could not register D-Bus object: %s",
               error ? error->message : "unknown");
}

static void
ooze_notifications_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                                  const char      *name G_GNUC_UNUSED,
                                  gpointer         user_data G_GNUC_UNUSED)
{
  g_print ("Ooze notifications: D-Bus server ready\n");
}

static void
ooze_notifications_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                              const char      *name G_GNUC_UNUSED,
                              gpointer         user_data)
{
  OozeNotifications *server = user_data;

  if (server->connection && server->registration_id)
    {
      g_dbus_connection_unregister_object (server->connection,
                                           server->registration_id);
      server->registration_id = 0;
    }
}

OozeNotifications *
ooze_notifications_new (OozePlugin *plugin)
{
  OozeNotifications *server;
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (OOZE_IS_PLUGIN (plugin), NULL);

  server = g_new0 (OozeNotifications, 1);
  server->plugin = plugin;
  server->next_id = 1;
  server->notifications =
    g_hash_table_new_full (g_direct_hash, g_direct_equal,
                           NULL, ooze_notification_free);
  server->order = g_ptr_array_new ();
  server->introspection =
    g_dbus_node_info_new_for_xml (notification_introspection_xml, &error);
  if (!server->introspection)
    {
      g_warning ("Ooze notifications: invalid D-Bus introspection: %s",
                 error ? error->message : "unknown");
      ooze_notifications_free (server);
      return NULL;
    }

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
  server->layer = clutter_actor_new ();
  clutter_actor_set_reactive (server->layer, FALSE);
  clutter_actor_add_child (stage, server->layer);

  server->own_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    OOZE_NOTIFICATIONS_BUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    ooze_notifications_bus_acquired,
                    ooze_notifications_name_acquired,
                    ooze_notifications_name_lost,
                    server,
                    NULL);
  return server;
}

void
ooze_notifications_free (OozeNotifications *server)
{
  if (!server)
    return;

  if (server->own_name_id)
    {
      g_bus_unown_name (server->own_name_id);
      server->own_name_id = 0;
    }
  if (server->connection && server->registration_id)
    {
      g_dbus_connection_unregister_object (server->connection,
                                           server->registration_id);
      server->registration_id = 0;
    }
  g_clear_object (&server->connection);

  if (server->notifications)
    g_hash_table_remove_all (server->notifications);
  g_clear_pointer (&server->order, g_ptr_array_unref);
  g_clear_pointer (&server->layer, clutter_actor_destroy);
  g_clear_pointer (&server->introspection, g_dbus_node_info_unref);
  g_clear_pointer (&server->notifications, g_hash_table_unref);
  g_free (server);
}
