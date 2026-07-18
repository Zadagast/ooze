#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE

#include "ooze-polkit.h"
#include "ooze-aqua-draw.h"
#include "ooze-theme.h"

#include "../common/ooze-font.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-plugin.h>

#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>

#include <polkitagent/polkitagent.h>

#define OOZE_POLKIT_CARD_W   420.0f
#define OOZE_POLKIT_CARD_H   250.0f
#define OOZE_POLKIT_ENTRY_W  300.0f
#define OOZE_POLKIT_ENTRY_H   34.0f
#define OOZE_POLKIT_BTN_W    130.0f
#define OOZE_POLKIT_BTN_H     32.0f
#define OOZE_POLKIT_MAX_TRIES 3

/* ── Listener type ───────────────────────────────────────────────────────── */

#define OOZE_TYPE_POLKIT_LISTENER (ooze_polkit_listener_get_type ())
G_DECLARE_FINAL_TYPE (OozePolkitListener, ooze_polkit_listener,
                      OOZE, POLKIT_LISTENER, PolkitAgentListener)

struct _OozePolkitListener
{
  PolkitAgentListener parent_instance;
};

G_DEFINE_TYPE (OozePolkitListener, ooze_polkit_listener,
               POLKIT_AGENT_TYPE_LISTENER)

/* ── Agent state ─────────────────────────────────────────────────────────── */

typedef struct
{
  OozePlugin *plugin;
  PolkitAgentListener *listener;
  gpointer register_handle;
  GCancellable *register_cancellable;

  /* Active authentication dialog (one at a time). */
  GTask *task;
  PolkitAgentSession *session;
  PolkitIdentity *identity;
  char *cookie;
  guint tries;
  gboolean dismissing;

  ClutterActor *overlay;
  ClutterActor *entry;
  ClutterActor *status_label;
  ClutterGrab *grab;
  GCancellable *auth_cancellable;
  gulong cancelled_handler;
} OozePolkitAgent;

static OozePolkitAgent agent;

static void ooze_polkit_session_start (void);
static void ooze_polkit_dialog_close (void);

/* ── Dialog teardown / task completion ──────────────────────────────────── */

static void
ooze_polkit_session_clear (void)
{
  if (!agent.session)
    return;

  g_signal_handlers_disconnect_by_data (agent.session, &agent);
  g_clear_object (&agent.session);
}

static void
ooze_polkit_dialog_close (void)
{
  ooze_polkit_session_clear ();

  if (agent.auth_cancellable && agent.cancelled_handler)
    {
      g_cancellable_disconnect (agent.auth_cancellable,
                                agent.cancelled_handler);
      agent.cancelled_handler = 0;
    }
  g_clear_object (&agent.auth_cancellable);

  if (agent.grab)
    {
      clutter_grab_dismiss (agent.grab);
      agent.grab = NULL;
    }

  if (agent.overlay)
    {
      clutter_actor_destroy (agent.overlay);
      agent.overlay = NULL;
    }
  agent.entry = NULL;
  agent.status_label = NULL;

  g_clear_object (&agent.identity);
  g_clear_pointer (&agent.cookie, g_free);
  agent.tries = 0;
  agent.dismissing = FALSE;
}

static void
ooze_polkit_finish_success (void)
{
  GTask *task = g_steal_pointer (&agent.task);

  ooze_polkit_dialog_close ();
  if (task)
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
    }
}

static void
ooze_polkit_finish_dismissed (void)
{
  GTask *task = g_steal_pointer (&agent.task);

  ooze_polkit_dialog_close ();
  if (task)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "Authentication dialog dismissed");
      g_object_unref (task);
    }
}

/* ── PolkitAgentSession signals ─────────────────────────────────────────── */

static void
ooze_polkit_set_status (const char *text)
{
  if (agent.status_label)
    clutter_text_set_text (CLUTTER_TEXT (agent.status_label),
                           text ? text : "");
}

static void
ooze_polkit_on_request (PolkitAgentSession *session G_GNUC_UNUSED,
                        const char         *request G_GNUC_UNUSED,
                        gboolean            echo_on,
                        gpointer            user_data G_GNUC_UNUSED)
{
  if (agent.entry)
    {
      clutter_text_set_password_char (CLUTTER_TEXT (agent.entry),
                                      echo_on ? 0 : 0x2022);
      clutter_actor_grab_key_focus (agent.entry);
    }
}

static void
ooze_polkit_on_show_error (PolkitAgentSession *session G_GNUC_UNUSED,
                           const char         *text,
                           gpointer            user_data G_GNUC_UNUSED)
{
  ooze_polkit_set_status (text);
}

static void
ooze_polkit_on_completed (PolkitAgentSession *session G_GNUC_UNUSED,
                          gboolean            gained_authorization,
                          gpointer            user_data G_GNUC_UNUSED)
{
  if (gained_authorization)
    {
      ooze_polkit_finish_success ();
      return;
    }

  if (agent.dismissing)
    {
      ooze_polkit_finish_dismissed ();
      return;
    }

  agent.tries++;
  if (agent.tries >= OOZE_POLKIT_MAX_TRIES)
    {
      ooze_polkit_finish_success ();
      return;
    }

  ooze_polkit_set_status ("Sorry, that didn't work. Try again.");
  if (agent.entry)
    {
      clutter_text_set_text (CLUTTER_TEXT (agent.entry), "");
      clutter_actor_grab_key_focus (agent.entry);
    }
  ooze_polkit_session_start ();
}

static void
ooze_polkit_session_start (void)
{
  ooze_polkit_session_clear ();

  if (!agent.identity || !agent.cookie)
    return;

  agent.session = polkit_agent_session_new (agent.identity, agent.cookie);
  g_signal_connect (agent.session, "request",
                    G_CALLBACK (ooze_polkit_on_request), &agent);
  g_signal_connect (agent.session, "show-error",
                    G_CALLBACK (ooze_polkit_on_show_error), &agent);
  g_signal_connect (agent.session, "show-info",
                    G_CALLBACK (ooze_polkit_on_show_error), &agent);
  g_signal_connect (agent.session, "completed",
                    G_CALLBACK (ooze_polkit_on_completed), &agent);
  polkit_agent_session_initiate (agent.session);
}

/* ── Dialog input ────────────────────────────────────────────────────────── */

static void
ooze_polkit_submit (void)
{
  const char *text;

  if (!agent.session || !agent.entry)
    return;

  text = clutter_text_get_text (CLUTTER_TEXT (agent.entry));
  ooze_polkit_set_status ("");
  polkit_agent_session_response (agent.session, text ? text : "");
}

static void
ooze_polkit_dismiss (void)
{
  if (agent.dismissing)
    return;
  agent.dismissing = TRUE;

  if (agent.session)
    polkit_agent_session_cancel (agent.session);
  else
    ooze_polkit_finish_dismissed ();
}

static void
ooze_polkit_on_entry_activate (ClutterText *text G_GNUC_UNUSED,
                               gpointer     user_data G_GNUC_UNUSED)
{
  ooze_polkit_submit ();
}

static gboolean
ooze_polkit_on_auth_clicked (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_polkit_submit ();
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_polkit_on_cancel_clicked (ClutterActor *actor G_GNUC_UNUSED,
                               ClutterEvent *event,
                               gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_polkit_dismiss ();
  return CLUTTER_EVENT_STOP;
}

static gboolean
ooze_polkit_on_overlay_key (ClutterActor *actor G_GNUC_UNUSED,
                            ClutterEvent *event,
                            gpointer      user_data G_GNUC_UNUSED)
{
  guint key = clutter_event_get_key_symbol (event);

  if (key == CLUTTER_KEY_Escape)
    {
      ooze_polkit_dismiss ();
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_polkit_on_overlay_button (ClutterActor *actor G_GNUC_UNUSED,
                               ClutterEvent *event,
                               gpointer      user_data G_GNUC_UNUSED)
{
  if (clutter_event_type (event) == CLUTTER_BUTTON_PRESS && agent.entry)
    clutter_actor_grab_key_focus (agent.entry);

  return CLUTTER_EVENT_STOP;
}

/* ── Dialog UI ───────────────────────────────────────────────────────────── */

static ClutterActor *
ooze_polkit_make_label (const char *font,
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
ooze_polkit_dialog_build (const char *message)
{
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;
  ClutterActor *card;
  ClutterActor *title;
  ClutterActor *body;
  ClutterActor *entry_bg;
  ClutterActor *auth_btn;
  ClutterActor *cancel_btn;
  g_autoptr (ClutterContent) card_content = NULL;
  g_autoptr (ClutterContent) entry_content = NULL;
  g_autoptr (ClutterContent) auth_content = NULL;
  g_autoptr (ClutterContent) cancel_content = NULL;
  CoglColor scrub;
  CoglColor text_color;
  MtkRectangle monitor;
  int stage_w = 0;
  int stage_h = 0;
  gfloat card_x;
  gfloat card_y;
  gboolean dark = ooze_theme_is_dark (NULL);
  gfloat text_r = dark ? 0.95f : 0.12f;
  gfloat text_g = dark ? 0.95f : 0.13f;
  gfloat text_b = dark ? 0.97f : 0.16f;

  display = meta_plugin_get_display (META_PLUGIN (agent.plugin));
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
  meta_display_get_size (display, &stage_w, &stage_h);
  ooze_plugin_get_active_monitor_geometry (display, &monitor);

  cogl_color_init_from_4f (&scrub, 0.0f, 0.0f, 0.0f, 0.35f);
  cogl_color_init_from_4f (&text_color, text_r, text_g, text_b, 1.0f);

  agent.overlay = clutter_actor_new ();
  clutter_actor_set_size (agent.overlay, (gfloat) stage_w, (gfloat) stage_h);
  clutter_actor_set_position (agent.overlay, 0.0f, 0.0f);
  clutter_actor_set_background_color (agent.overlay, &scrub);
  clutter_actor_set_reactive (agent.overlay, TRUE);
  clutter_actor_add_child (stage, agent.overlay);
  clutter_actor_set_child_above_sibling (stage, agent.overlay, NULL);

  g_signal_connect (agent.overlay, "key-press-event",
                    G_CALLBACK (ooze_polkit_on_overlay_key), &agent);
  g_signal_connect (agent.overlay, "button-press-event",
                    G_CALLBACK (ooze_polkit_on_overlay_button), &agent);

  card = clutter_actor_new ();
  clutter_actor_set_size (card, OOZE_POLKIT_CARD_W, OOZE_POLKIT_CARD_H);
  card_content = ooze_aqua_squircle_panel_content (card,
                                                   (int) OOZE_POLKIT_CARD_W,
                                                   (int) OOZE_POLKIT_CARD_H,
                                                   FALSE);
  if (card_content)
    clutter_actor_set_content (card, g_steal_pointer (&card_content));
  clutter_actor_set_reactive (card, TRUE);
  clutter_actor_add_child (agent.overlay, card);

  title = ooze_polkit_make_label (OOZE_UI_FONT_EMPHASIS, "Authentication Required",
                                  text_r, text_g, text_b);
  clutter_actor_add_child (card, title);

  body = ooze_polkit_make_label (OOZE_UI_FONT,
                                 message && *message
                                   ? message
                                   : "An application needs to authenticate.",
                                 text_r, text_g, text_b);
  clutter_text_set_line_wrap (CLUTTER_TEXT (body), TRUE);
  clutter_actor_set_width (body, OOZE_POLKIT_CARD_W - 48.0f);
  clutter_actor_add_child (card, body);

  entry_bg = clutter_actor_new ();
  clutter_actor_set_size (entry_bg, OOZE_POLKIT_ENTRY_W, OOZE_POLKIT_ENTRY_H);
  entry_content = ooze_aqua_squircle_panel_content (entry_bg,
                                                    (int) OOZE_POLKIT_ENTRY_W,
                                                    (int) OOZE_POLKIT_ENTRY_H,
                                                    TRUE);
  if (entry_content)
    clutter_actor_set_content (entry_bg, g_steal_pointer (&entry_content));
  clutter_actor_set_reactive (entry_bg, TRUE);
  clutter_actor_add_child (card, entry_bg);

  agent.entry = clutter_text_new ();
  clutter_text_set_editable (CLUTTER_TEXT (agent.entry), TRUE);
  clutter_text_set_activatable (CLUTTER_TEXT (agent.entry), TRUE);
  clutter_text_set_password_char (CLUTTER_TEXT (agent.entry), 0x2022);
  clutter_text_set_single_line_mode (CLUTTER_TEXT (agent.entry), TRUE);
  clutter_text_set_font_name (CLUTTER_TEXT (agent.entry), OOZE_UI_FONT);
  clutter_text_set_color (CLUTTER_TEXT (agent.entry), &text_color);
  clutter_text_set_cursor_color (CLUTTER_TEXT (agent.entry), &text_color);
  clutter_actor_set_reactive (agent.entry, TRUE);
  clutter_actor_set_size (agent.entry,
                          OOZE_POLKIT_ENTRY_W - 24.0f,
                          OOZE_POLKIT_ENTRY_H - 10.0f);
  clutter_actor_add_child (entry_bg, agent.entry);
  clutter_actor_set_position (agent.entry, 12.0f, 5.0f);
  g_signal_connect (agent.entry, "activate",
                    G_CALLBACK (ooze_polkit_on_entry_activate), &agent);

  cancel_btn = clutter_actor_new ();
  clutter_actor_set_size (cancel_btn, OOZE_POLKIT_BTN_W, OOZE_POLKIT_BTN_H);
  cancel_content = ooze_aqua_kit_button_content (cancel_btn, "Cancel",
                                                 (int) OOZE_POLKIT_BTN_W,
                                                 (int) OOZE_POLKIT_BTN_H);
  if (cancel_content)
    clutter_actor_set_content (cancel_btn, g_steal_pointer (&cancel_content));
  clutter_actor_set_reactive (cancel_btn, TRUE);
  clutter_actor_add_child (card, cancel_btn);
  g_signal_connect (cancel_btn, "button-press-event",
                    G_CALLBACK (ooze_polkit_on_cancel_clicked), &agent);

  auth_btn = clutter_actor_new ();
  clutter_actor_set_size (auth_btn, OOZE_POLKIT_BTN_W, OOZE_POLKIT_BTN_H);
  auth_content = ooze_aqua_kit_button_content (auth_btn, "Authenticate",
                                               (int) OOZE_POLKIT_BTN_W,
                                               (int) OOZE_POLKIT_BTN_H);
  if (auth_content)
    clutter_actor_set_content (auth_btn, g_steal_pointer (&auth_content));
  clutter_actor_set_reactive (auth_btn, TRUE);
  clutter_actor_add_child (card, auth_btn);
  g_signal_connect (auth_btn, "button-press-event",
                    G_CALLBACK (ooze_polkit_on_auth_clicked), &agent);

  agent.status_label = ooze_polkit_make_label (OOZE_UI_FONT, "",
                                               0.92f, 0.45f, 0.38f);
  clutter_actor_add_child (card, agent.status_label);

  card_x = monitor.x + ((gfloat) monitor.width - OOZE_POLKIT_CARD_W) / 2.0f;
  card_y = monitor.y + ((gfloat) monitor.height - OOZE_POLKIT_CARD_H) / 2.0f;
  clutter_actor_set_position (card, card_x, card_y);
  clutter_actor_set_position (title,
                              (OOZE_POLKIT_CARD_W -
                               clutter_actor_get_width (title)) / 2.0f,
                              24.0f);
  clutter_actor_set_position (body, 24.0f, 56.0f);
  clutter_actor_set_position (entry_bg,
                              (OOZE_POLKIT_CARD_W - OOZE_POLKIT_ENTRY_W) / 2.0f,
                              120.0f);
  clutter_actor_set_position (cancel_btn,
                              OOZE_POLKIT_CARD_W / 2.0f -
                                OOZE_POLKIT_BTN_W - 8.0f,
                              170.0f);
  clutter_actor_set_position (auth_btn,
                              OOZE_POLKIT_CARD_W / 2.0f + 8.0f,
                              170.0f);
  clutter_actor_set_position (agent.status_label, 24.0f, 214.0f);

  agent.grab = clutter_stage_grab (CLUTTER_STAGE (stage), agent.overlay);
  clutter_actor_grab_key_focus (agent.entry);
}

/* ── Listener vfuncs ─────────────────────────────────────────────────────── */

static PolkitIdentity *
ooze_polkit_pick_identity (GList *identities)
{
  GList *l;

  for (l = identities; l; l = l->next)
    {
      PolkitIdentity *identity = l->data;

      if (POLKIT_IS_UNIX_USER (identity) &&
          (uid_t) polkit_unix_user_get_uid (POLKIT_UNIX_USER (identity)) ==
            getuid ())
        return identity;
    }

  return identities ? identities->data : NULL;
}

static void
ooze_polkit_on_cancelled (GCancellable *cancellable G_GNUC_UNUSED,
                          gpointer      user_data G_GNUC_UNUSED)
{
  ooze_polkit_dismiss ();
}

static void
ooze_polkit_initiate_authentication (PolkitAgentListener *listener,
                                     const char          *action_id G_GNUC_UNUSED,
                                     const char          *message,
                                     const char          *icon_name G_GNUC_UNUSED,
                                     PolkitDetails       *details G_GNUC_UNUSED,
                                     const char          *cookie,
                                     GList               *identities,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  GTask *task;
  PolkitIdentity *identity;

  task = g_task_new (listener, cancellable, callback, user_data);
  g_task_set_source_tag (task, ooze_polkit_initiate_authentication);

  if (agent.task)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_BUSY,
                               "An authentication dialog is already open");
      g_object_unref (task);
      return;
    }

  identity = ooze_polkit_pick_identity (identities);
  if (!identity || !agent.plugin || agent.plugin->shutting_down)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Cannot authenticate now");
      g_object_unref (task);
      return;
    }

  agent.task = task;
  agent.identity = g_object_ref (identity);
  agent.cookie = g_strdup (cookie);
  agent.tries = 0;
  agent.dismissing = FALSE;

  if (cancellable)
    {
      agent.auth_cancellable = g_object_ref (cancellable);
      agent.cancelled_handler =
        g_cancellable_connect (cancellable,
                               G_CALLBACK (ooze_polkit_on_cancelled),
                               NULL, NULL);
    }

  ooze_polkit_dialog_build (message);
  ooze_polkit_session_start ();
}

static gboolean
ooze_polkit_initiate_authentication_finish (PolkitAgentListener *listener G_GNUC_UNUSED,
                                            GAsyncResult        *res,
                                            GError             **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
ooze_polkit_listener_class_init (OozePolkitListenerClass *klass)
{
  PolkitAgentListenerClass *listener_class =
    POLKIT_AGENT_LISTENER_CLASS (klass);

  listener_class->initiate_authentication =
    ooze_polkit_initiate_authentication;
  listener_class->initiate_authentication_finish =
    ooze_polkit_initiate_authentication_finish;
}

static void
ooze_polkit_listener_init (OozePolkitListener *self G_GNUC_UNUSED)
{
}

/* ── Registration ────────────────────────────────────────────────────────── */

static void
ooze_polkit_subject_got (GObject      *source G_GNUC_UNUSED,
                         GAsyncResult *res,
                         gpointer      user_data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (PolkitSubject) subject = NULL;

  subject = polkit_unix_session_new_for_process_finish (res, &error);
  if (!subject)
    {
      g_warning ("Ooze polkit: no login session subject: %s",
                 error->message);
      return;
    }

  if (!agent.plugin || agent.plugin->shutting_down || !agent.listener)
    return;

  agent.register_handle =
    polkit_agent_listener_register (agent.listener,
                                    POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                    subject,
                                    NULL,
                                    NULL,
                                    &error);
  if (!agent.register_handle)
    g_warning ("Ooze polkit: failed to register agent: %s", error->message);
  else
    g_print ("Ooze polkit: authentication agent registered\n");
}

void
ooze_polkit_init (OozePlugin *plugin)
{
  /* Nested devkit shares the host session: its agent already handles it. */
  if (g_strcmp0 (g_getenv ("OOZE_DEVKIT"), "1") == 0)
    return;

  if (agent.plugin)
    return;

  agent.plugin = plugin;
  agent.listener = g_object_new (OOZE_TYPE_POLKIT_LISTENER, NULL);
  agent.register_cancellable = g_cancellable_new ();

  polkit_unix_session_new_for_process (getpid (),
                                       agent.register_cancellable,
                                       ooze_polkit_subject_got,
                                       NULL);
}

void
ooze_polkit_shutdown (void)
{
  if (agent.register_cancellable)
    {
      g_cancellable_cancel (agent.register_cancellable);
      g_clear_object (&agent.register_cancellable);
    }

  if (agent.task)
    {
      agent.dismissing = TRUE;
      ooze_polkit_finish_dismissed ();
    }

  if (agent.register_handle)
    {
      polkit_agent_listener_unregister (agent.register_handle);
      agent.register_handle = NULL;
    }

  g_clear_object (&agent.listener);
  agent.plugin = NULL;
}
