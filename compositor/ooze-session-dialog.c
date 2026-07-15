#include "ooze-session-dialog.h"
#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-theme.h"

#include "../common/ooze-font.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-plugin.h>
#include <meta/window.h>

#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>
#include <gio/gio.h>

#define OOZE_SESSION_CARD_W    440.0f
#define OOZE_SESSION_CARD_H    210.0f
#define OOZE_SESSION_BTN_W     150.0f
#define OOZE_SESSION_BTN_H      34.0f
#define OOZE_SESSION_COUNTDOWN  60
#define OOZE_LOGIND_TIMEOUT_MS  5000
/* Give apps a moment to close their windows before the compositor exits. */
#define OOZE_SESSION_LOGOUT_GRACE_MS 800

typedef struct
{
  OozePlugin       *plugin;
  OozeSessionAction action;

  ClutterActor *overlay;
  ClutterActor *message_label;
  ClutterGrab  *grab;

  guint countdown;
  guint countdown_timer;
  guint logout_timer;
} OozeSessionDialog;

static OozeSessionDialog dialog;

static void ooze_session_dialog_confirm (void);

/* ── Action strings ──────────────────────────────────────────────────────── */

static const char *
ooze_session_action_title (OozeSessionAction action)
{
  switch (action)
    {
    case OOZE_SESSION_ACTION_RESTART:  return "Restart";
    case OOZE_SESSION_ACTION_SHUTDOWN: return "Shut Down";
    case OOZE_SESSION_ACTION_SUSPEND:  return "Suspend";
    case OOZE_SESSION_ACTION_LOGOUT:
    default:                           return "Log Out";
  }
}

static const char *
ooze_session_action_verb (OozeSessionAction action)
{
  switch (action)
    {
    case OOZE_SESSION_ACTION_RESTART:  return "The computer will restart";
    case OOZE_SESSION_ACTION_SHUTDOWN: return "The computer will turn off";
    case OOZE_SESSION_ACTION_SUSPEND:  return "The computer will suspend";
    case OOZE_SESSION_ACTION_LOGOUT:
    default:                           return "You will be logged out";
  }
}

/* ── logind ──────────────────────────────────────────────────────────────── */

static void
ooze_session_logind_done (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  g_autofree char *method = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res,
                                         &error);
  if (error)
    g_warning ("Ooze session: logind %s failed: %s", method, error->message);
}

static void
ooze_session_logind_bus_got (GObject      *source G_GNUC_UNUSED,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  char *method = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) conn = NULL;

  conn = g_bus_get_finish (res, &error);
  if (!conn)
    {
      g_warning ("Ooze session: logind unavailable: %s", error->message);
      g_free (method);
      return;
    }

  g_dbus_connection_call (conn,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          method,
                          g_variant_new ("(b)", TRUE),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          OOZE_LOGIND_TIMEOUT_MS,
                          NULL,
                          ooze_session_logind_done,
                          method);
}

static void
ooze_session_logind_call (const char *method)
{
  g_bus_get (G_BUS_TYPE_SYSTEM, NULL, ooze_session_logind_bus_got,
             g_strdup (method));
}

/* ── Logout (clean compositor exit) ─────────────────────────────────────── */

static void
ooze_session_close_windows (OozePlugin *plugin)
{
  MetaDisplay *display;
  GList *windows;
  GList *l;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  windows = meta_display_list_all_windows (display);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = l->data;

      if (meta_window_get_window_type (window) == META_WINDOW_NORMAL)
        meta_window_delete (window, clutter_get_current_event_time ());
    }
  g_list_free (windows);
}

static gboolean
ooze_session_logout_finish (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  dialog.logout_timer = 0;
  if (plugin->shutting_down)
    return G_SOURCE_REMOVE;

  ooze_plugin_begin_shutdown (plugin);
  if (plugin->context)
    meta_context_terminate (plugin->context);

  return G_SOURCE_REMOVE;
}

/* ── Teardown ────────────────────────────────────────────────────────────── */

void
ooze_session_dialog_dismiss (void)
{
  if (dialog.countdown_timer)
    {
      g_source_remove (dialog.countdown_timer);
      dialog.countdown_timer = 0;
    }

  if (dialog.grab)
    {
      clutter_grab_dismiss (dialog.grab);
      dialog.grab = NULL;
    }

  if (dialog.overlay)
    {
      clutter_actor_destroy (dialog.overlay);
      dialog.overlay = NULL;
    }

  dialog.message_label = NULL;
}

static void
ooze_session_dialog_confirm (void)
{
  OozePlugin *plugin = dialog.plugin;
  OozeSessionAction action = dialog.action;

  /* Drop the grab/overlay before terminating so nothing is grabbing when
   * Mutter disposes the backend (see the logout-crash fix, #57). */
  ooze_session_dialog_dismiss ();

  switch (action)
    {
    case OOZE_SESSION_ACTION_RESTART:
      ooze_session_logind_call ("Reboot");
      break;

    case OOZE_SESSION_ACTION_SHUTDOWN:
      ooze_session_logind_call ("PowerOff");
      break;

    case OOZE_SESSION_ACTION_SUSPEND:
      ooze_session_logind_call ("Suspend");
      break;

    case OOZE_SESSION_ACTION_LOGOUT:
    default:
      if (plugin && !plugin->shutting_down && !dialog.logout_timer)
        {
          g_print ("Ooze: ending session\n");
          ooze_session_close_windows (plugin);
          dialog.logout_timer =
            g_timeout_add_full (G_PRIORITY_LOW,
                                OOZE_SESSION_LOGOUT_GRACE_MS,
                                ooze_session_logout_finish, plugin, NULL);
        }
      break;
    }
}

/* ── Countdown ───────────────────────────────────────────────────────────── */

static void
ooze_session_update_message (void)
{
  g_autofree char *text = NULL;

  if (!dialog.message_label)
    return;

  text = g_strdup_printf ("%s automatically in %u second%s.",
                          ooze_session_action_verb (dialog.action),
                          dialog.countdown,
                          dialog.countdown == 1 ? "" : "s");
  clutter_text_set_text (CLUTTER_TEXT (dialog.message_label), text);
}

static gboolean
ooze_session_countdown_tick (gpointer user_data G_GNUC_UNUSED)
{
  if (dialog.countdown == 0)
    {
      dialog.countdown_timer = 0;
      ooze_session_dialog_confirm ();
      return G_SOURCE_REMOVE;
    }

  dialog.countdown--;
  ooze_session_update_message ();

  if (dialog.countdown == 0)
    {
      dialog.countdown_timer = 0;
      ooze_session_dialog_confirm ();
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

/* ── Input ───────────────────────────────────────────────────────────────── */

static gboolean
ooze_session_on_confirm_clicked (ClutterActor *actor G_GNUC_UNUSED,
                                 ClutterEvent *event,
                                 gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_session_dialog_confirm ();
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_session_on_cancel_clicked (ClutterActor *actor G_GNUC_UNUSED,
                                ClutterEvent *event,
                                gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_session_dialog_dismiss ();
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_session_on_overlay_key (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             gpointer      user_data G_GNUC_UNUSED)
{
  guint key = clutter_event_get_key_symbol (event);

  if (key == CLUTTER_KEY_Escape)
    {
      ooze_session_dialog_dismiss ();
      return CLUTTER_EVENT_STOP;
    }

  if (key == CLUTTER_KEY_Return || key == CLUTTER_KEY_KP_Enter)
    {
      ooze_session_dialog_confirm ();
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_session_on_overlay_button (ClutterActor *actor G_GNUC_UNUSED,
                                ClutterEvent *event G_GNUC_UNUSED,
                                gpointer      user_data G_GNUC_UNUSED)
{
  return CLUTTER_EVENT_STOP;
}

/* ── UI ──────────────────────────────────────────────────────────────────── */

static ClutterActor *
ooze_session_make_label (const char *font,
                         const char *text,
                         gfloat      r,
                         gfloat      g,
                         gfloat      b)
{
  ClutterActor *label = clutter_text_new ();
  CoglColor color;

  cogl_color_init_from_4f (&color, r, g, b, 1.0f);
  clutter_text_set_font_name (CLUTTER_TEXT (label), font);
  clutter_text_set_text (CLUTTER_TEXT (label), text);
  clutter_text_set_color (CLUTTER_TEXT (label), &color);
  return label;
}

static void
ooze_session_dialog_build (void)
{
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;
  ClutterActor *card;
  ClutterActor *title;
  ClutterActor *confirm_btn;
  ClutterActor *cancel_btn;
  g_autoptr (ClutterContent) card_content = NULL;
  g_autoptr (ClutterContent) confirm_content = NULL;
  g_autoptr (ClutterContent) cancel_content = NULL;
  CoglColor scrub;
  int stage_w = 0;
  int stage_h = 0;
  gfloat card_x;
  gfloat card_y;
  gboolean dark = ooze_theme_is_dark (NULL);
  gfloat text_r = dark ? 0.95f : 0.12f;
  gfloat text_g = dark ? 0.95f : 0.13f;
  gfloat text_b = dark ? 0.97f : 0.16f;

  display = meta_plugin_get_display (META_PLUGIN (dialog.plugin));
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
  meta_display_get_size (display, &stage_w, &stage_h);

  cogl_color_init_from_4f (&scrub, 0.0f, 0.0f, 0.0f, 0.45f);

  dialog.overlay = clutter_actor_new ();
  clutter_actor_set_size (dialog.overlay, (gfloat) stage_w, (gfloat) stage_h);
  clutter_actor_set_position (dialog.overlay, 0.0f, 0.0f);
  clutter_actor_set_background_color (dialog.overlay, &scrub);
  clutter_actor_set_reactive (dialog.overlay, TRUE);
  clutter_actor_add_child (stage, dialog.overlay);
  clutter_actor_set_child_above_sibling (stage, dialog.overlay, NULL);

  g_signal_connect (dialog.overlay, "key-press-event",
                    G_CALLBACK (ooze_session_on_overlay_key), NULL);
  g_signal_connect (dialog.overlay, "button-press-event",
                    G_CALLBACK (ooze_session_on_overlay_button), NULL);

  card = clutter_actor_new ();
  clutter_actor_set_size (card, OOZE_SESSION_CARD_W, OOZE_SESSION_CARD_H);
  card_content = ooze_aqua_squircle_panel_content (card,
                                                   (int) OOZE_SESSION_CARD_W,
                                                   (int) OOZE_SESSION_CARD_H,
                                                   FALSE);
  if (card_content)
    clutter_actor_set_content (card, g_steal_pointer (&card_content));
  clutter_actor_set_reactive (card, TRUE);
  clutter_actor_add_child (dialog.overlay, card);

  title = ooze_session_make_label (OOZE_UI_FONT_EMPHASIS,
                                   ooze_session_action_title (dialog.action),
                                   text_r, text_g, text_b);
  clutter_actor_add_child (card, title);

  dialog.message_label = ooze_session_make_label (OOZE_UI_FONT, "",
                                                  text_r, text_g, text_b);
  clutter_text_set_line_wrap (CLUTTER_TEXT (dialog.message_label), TRUE);
  clutter_actor_set_width (dialog.message_label, OOZE_SESSION_CARD_W - 48.0f);
  clutter_actor_add_child (card, dialog.message_label);
  ooze_session_update_message ();

  cancel_btn = clutter_actor_new ();
  clutter_actor_set_size (cancel_btn, OOZE_SESSION_BTN_W, OOZE_SESSION_BTN_H);
  cancel_content = ooze_aqua_kit_button_content (cancel_btn, "Cancel",
                                                 (int) OOZE_SESSION_BTN_W,
                                                 (int) OOZE_SESSION_BTN_H);
  if (cancel_content)
    clutter_actor_set_content (cancel_btn, g_steal_pointer (&cancel_content));
  clutter_actor_set_reactive (cancel_btn, TRUE);
  clutter_actor_add_child (card, cancel_btn);
  g_signal_connect (cancel_btn, "button-press-event",
                    G_CALLBACK (ooze_session_on_cancel_clicked), NULL);

  confirm_btn = clutter_actor_new ();
  clutter_actor_set_size (confirm_btn, OOZE_SESSION_BTN_W, OOZE_SESSION_BTN_H);
  confirm_content =
    ooze_aqua_kit_button_content (confirm_btn,
                                  ooze_session_action_title (dialog.action),
                                  (int) OOZE_SESSION_BTN_W,
                                  (int) OOZE_SESSION_BTN_H);
  if (confirm_content)
    clutter_actor_set_content (confirm_btn, g_steal_pointer (&confirm_content));
  clutter_actor_set_reactive (confirm_btn, TRUE);
  clutter_actor_add_child (card, confirm_btn);
  g_signal_connect (confirm_btn, "button-press-event",
                    G_CALLBACK (ooze_session_on_confirm_clicked), NULL);

  card_x = ((gfloat) stage_w - OOZE_SESSION_CARD_W) / 2.0f;
  card_y = ((gfloat) stage_h - OOZE_SESSION_CARD_H) / 2.0f;
  clutter_actor_set_position (card, card_x, card_y);
  clutter_actor_set_position (title,
                              (OOZE_SESSION_CARD_W -
                               clutter_actor_get_width (title)) / 2.0f,
                              28.0f);
  clutter_actor_set_position (dialog.message_label, 24.0f, 76.0f);
  clutter_actor_set_position (cancel_btn,
                              OOZE_SESSION_CARD_W / 2.0f -
                                OOZE_SESSION_BTN_W - 8.0f,
                              150.0f);
  clutter_actor_set_position (confirm_btn,
                              OOZE_SESSION_CARD_W / 2.0f + 8.0f,
                              150.0f);

  dialog.grab = clutter_stage_grab (CLUTTER_STAGE (stage), dialog.overlay);
  clutter_actor_grab_key_focus (dialog.overlay);
}

void
ooze_session_dialog_present (OozePlugin        *plugin,
                             OozeSessionAction  action)
{
  if (!plugin || plugin->shutting_down)
    return;

  /* Replace any dialog already showing (e.g. a different action). */
  ooze_session_dialog_dismiss ();

  dialog.plugin = plugin;
  dialog.action = action;
  dialog.countdown = OOZE_SESSION_COUNTDOWN;

  ooze_session_dialog_build ();

  dialog.countdown_timer =
    g_timeout_add_seconds (1, ooze_session_countdown_tick, NULL);
}
