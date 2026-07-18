/*
 * Ooze lock screen — fullscreen Clutter overlay inside Mutter.
 *
 * Approach A: compositor overlay + clutter_stage_grab + async PAM helper.
 * Mutter has no ext-session-lock-v1; gtklock/swaylock cannot lock this session.
 */

#include "ooze-lock.h"
#include "ooze-plugin-priv.h"
#include "ooze-theme.h"
#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-screensaver.h"

#include "../common/ooze-font.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/meta-idle-monitor.h>
#include <meta/meta-plugin.h>
#include <clutter/clutter.h>
#include <clutter/clutter-pango.h>

#include <gio/gio.h>
#include <glib.h>
#include <string.h>
#include <time.h>

#define OOZE_LOGIND_TIMEOUT_MS   8000
#define OOZE_LOCK_CARD_W         380.0f
#define OOZE_LOCK_CARD_H         248.0f
#define OOZE_LOCK_ENTRY_W        300.0f
#define OOZE_LOCK_ENTRY_H         40.0f
#define OOZE_LOCK_BTN_W          148.0f
#define OOZE_LOCK_BTN_H           36.0f
#define OOZE_LOCK_CLOCK_FONT     "Inter Bold 48"
#define OOZE_LOCK_USER_FONT      "Inter Medium 15"
#define OOZE_LOCK_STATUS_FONT    "Inter 12"
#define OOZE_LOCK_SHAKE_MS        280
#define OOZE_LOCK_HELPER_NAME    "ooze-pam-helper"

static void ooze_lock_dismiss (OozePlugin *plugin, gboolean authenticated);
static void ooze_lock_set_status (OozePlugin *plugin, const char *text);
static void ooze_lock_sync_idle_watch (OozePlugin *plugin);
static void ooze_lock_set_locked_hint (gboolean locked);

static ClutterActor *
lock_make_label (ClutterActor *ref,
                 const char   *font,
                 const char   *text,
                 gfloat        r,
                 gfloat        g,
                 gfloat        b)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  content = ooze_aqua_text_content (ref, font, text, r, g, b, &width, &height);
  actor = clutter_actor_new ();
  if (content)
    ooze_aqua_actor_set_content (actor, g_steal_pointer (&content), width, height);
  else
    clutter_actor_set_size (actor, 1.0f, 1.0f);
  return actor;
}

static void
lock_set_label (ClutterActor *actor,
                const char   *font,
                const char   *text,
                gfloat        r,
                gfloat        g,
                gfloat        b)
{
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  if (!actor)
    return;

  content = ooze_aqua_text_content (actor, font, text, r, g, b, &width, &height);
  if (!content)
    return;
  ooze_aqua_actor_set_content (actor, g_steal_pointer (&content), width, height);
}

static char *
ooze_lock_find_helper (void)
{
  const char *env;
  g_autofree char *exe = NULL;
  g_autofree char *dir = NULL;
  g_autofree char *beside = NULL;
  g_autofree char *path = NULL;

  env = g_getenv ("OOZE_PAM_HELPER");
  if (env && env[0] && g_file_test (env, G_FILE_TEST_IS_EXECUTABLE))
    return g_strdup (env);

  exe = g_file_read_link ("/proc/self/exe", NULL);
  if (exe)
    {
      dir = g_path_get_dirname (exe);
      beside = g_build_filename (dir, OOZE_LOCK_HELPER_NAME, NULL);
      if (g_file_test (beside, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&beside);
    }

  path = g_find_program_in_path (OOZE_LOCK_HELPER_NAME);
  if (path)
    return g_steal_pointer (&path);

  return NULL;
}

typedef struct
{
  char     *method;
  GVariant *params;
} OozeLockLogindCall;

static void
ooze_lock_logind_done (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  OozeLockLogindCall *call = user_data;
  g_autoptr (GError) error = NULL;

  g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
  if (error)
    g_warning ("Ooze lock: logind %s failed: %s",
               call->method ? call->method : "?", error->message);

  g_free (call->method);
  if (call->params)
    g_variant_unref (call->params);
  g_free (call);
}

static void
ooze_lock_logind_bus_got (GObject      *source G_GNUC_UNUSED,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  OozeLockLogindCall *call = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) conn = NULL;

  conn = g_bus_get_finish (res, &error);
  if (!conn)
    {
      g_warning ("Ooze lock: system bus unavailable: %s", error->message);
      g_free (call->method);
      if (call->params)
        g_variant_unref (call->params);
      g_free (call);
      return;
    }

  g_dbus_connection_call (conn,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1/session/auto",
                          "org.freedesktop.login1.Session",
                          call->method,
                          call->params,
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          OOZE_LOGIND_TIMEOUT_MS,
                          NULL,
                          ooze_lock_logind_done,
                          call);
}

static void
ooze_lock_session_call (const char *method,
                        GVariant   *params)
{
  OozeLockLogindCall *call;

  call = g_new0 (OozeLockLogindCall, 1);
  call->method = g_strdup (method);
  call->params = params; /* floating or full; sink if floating */
  if (params && g_variant_is_floating (params))
    g_variant_ref_sink (params);

  g_bus_get (G_BUS_TYPE_SYSTEM, NULL, ooze_lock_logind_bus_got, call);
}

static void
ooze_lock_set_locked_hint (gboolean locked)
{
  ooze_lock_session_call ("SetLockedHint", g_variant_new ("(b)", locked));
}

static void
ooze_lock_update_clock (OozePlugin *plugin)
{
  time_t now;
  struct tm *tm_local;
  char buffer[64];

  if (!plugin->lock_clock_label)
    return;

  now = time (NULL);
  tm_local = localtime (&now);
  if (!tm_local)
    return;

  strftime (buffer, sizeof buffer, "%-l:%M", tm_local);
  {
    gboolean dark = ooze_theme_is_dark (NULL);
    gfloat r = dark ? 0.95f : 0.12f;
    gfloat g = dark ? 0.95f : 0.13f;
    gfloat b = dark ? 0.97f : 0.16f;

    lock_set_label (plugin->lock_clock_label,
                    OOZE_LOCK_CLOCK_FONT,
                    buffer,
                    r, g, b);
  }
}

static gboolean
ooze_lock_on_clock_tick (gpointer user_data)
{
  ooze_lock_update_clock (OOZE_PLUGIN (user_data));
  return G_SOURCE_CONTINUE;
}

static void
ooze_lock_set_status (OozePlugin *plugin, const char *text)
{
  lock_set_label (plugin->lock_status_label,
                  OOZE_LOCK_STATUS_FONT,
                  text ? text : "",
                  0.92f, 0.55f, 0.50f);

  if (plugin->lock_status_label && plugin->lock_card)
    clutter_actor_set_x (plugin->lock_status_label,
                         (OOZE_LOCK_CARD_W -
                          clutter_actor_get_width (plugin->lock_status_label)) /
                         2.0f);
}

static void
ooze_lock_shake_entry (OozePlugin *plugin)
{
  gfloat x;
  gfloat y;

  if (!plugin->lock_entry_box)
    return;

  x = clutter_actor_get_x (plugin->lock_entry_box);
  y = clutter_actor_get_y (plugin->lock_entry_box);

  clutter_actor_remove_all_transitions (plugin->lock_entry_box);
  clutter_actor_set_position (plugin->lock_entry_box, x, y);

  clutter_actor_save_easing_state (plugin->lock_entry_box);
  clutter_actor_set_easing_mode (plugin->lock_entry_box, CLUTTER_EASE_OUT_BOUNCE);
  clutter_actor_set_easing_duration (plugin->lock_entry_box, OOZE_LOCK_SHAKE_MS);
  clutter_actor_set_x (plugin->lock_entry_box, x + 18.0f);
  clutter_actor_restore_easing_state (plugin->lock_entry_box);

  clutter_actor_save_easing_state (plugin->lock_entry_box);
  clutter_actor_set_easing_delay (plugin->lock_entry_box, OOZE_LOCK_SHAKE_MS / 2);
  clutter_actor_set_easing_mode (plugin->lock_entry_box, CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_easing_duration (plugin->lock_entry_box, OOZE_LOCK_SHAKE_MS / 2);
  clutter_actor_set_x (plugin->lock_entry_box, x);
  clutter_actor_restore_easing_state (plugin->lock_entry_box);
}

static void
ooze_lock_auth_finished (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) stdout_buf = NULL;
  g_autoptr (GBytes) stderr_buf = NULL;
  int status = 1;

  g_clear_object (&plugin->lock_auth_proc);
  if (plugin->shutting_down)
    return;

  if (!g_subprocess_communicate_finish (G_SUBPROCESS (source),
                                        res,
                                        &stdout_buf,
                                        &stderr_buf,
                                        &error))
    {
      g_warning ("Ooze lock: PAM helper I/O failed: %s", error->message);
      ooze_lock_set_status (plugin, "Unlock unavailable");
      if (plugin->lock_password)
        {
          clutter_text_set_editable (CLUTTER_TEXT (plugin->lock_password), TRUE);
          clutter_actor_grab_key_focus (plugin->lock_password);
        }
      return;
    }

  if (g_subprocess_get_if_exited (G_SUBPROCESS (source)))
    status = g_subprocess_get_exit_status (G_SUBPROCESS (source));
  else
    status = 2;

  if (status == 0)
    {
      ooze_lock_dismiss (plugin, TRUE);
      return;
    }

  if (status == 1)
    ooze_lock_set_status (plugin, "Incorrect password");
  else
    ooze_lock_set_status (plugin, "Unlock failed");

  ooze_lock_shake_entry (plugin);

  if (plugin->lock_password)
    {
      clutter_text_set_text (CLUTTER_TEXT (plugin->lock_password), "");
      clutter_text_set_editable (CLUTTER_TEXT (plugin->lock_password), TRUE);
      clutter_actor_grab_key_focus (plugin->lock_password);
    }
}

static void
ooze_lock_try_unlock (OozePlugin *plugin)
{
  g_autofree char *helper = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *proc;
  const char *password;
  GBytes *stdin_bytes;

  if (!plugin->locked || plugin->lock_auth_proc)
    return;

  if (!plugin->lock_password)
    return;

  password = clutter_text_get_text (CLUTTER_TEXT (plugin->lock_password));
  if (!password || password[0] == '\0')
    {
      ooze_lock_set_status (plugin, "Enter password");
      ooze_lock_shake_entry (plugin);
      return;
    }

  helper = ooze_lock_find_helper ();
  if (!helper)
    {
      g_warning ("Ooze lock: %s not found — soft-fail, stay locked",
                 OOZE_LOCK_HELPER_NAME);
      ooze_lock_set_status (plugin, "Unlock helper missing");
      return;
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                        G_SUBPROCESS_FLAGS_STDERR_PIPE);
  proc = g_subprocess_launcher_spawn (launcher, &error,
                                      helper,
                                      g_get_user_name (),
                                      NULL);
  if (!proc)
    {
      g_warning ("Ooze lock: failed to spawn helper: %s", error->message);
      ooze_lock_set_status (plugin, "Unlock helper failed");
      return;
    }

  plugin->lock_auth_proc = g_steal_pointer (&proc);
  clutter_text_set_editable (CLUTTER_TEXT (plugin->lock_password), FALSE);
  ooze_lock_set_status (plugin, "Unlocking…");

  {
    gsize len = strlen (password);
    guint8 *buf = g_malloc (len + 1);

    memcpy (buf, password, len);
    buf[len] = '\0';
    stdin_bytes = g_bytes_new_take (buf, len + 1);
  }

  /* Clear visible text immediately so it cannot be read back. */
  clutter_text_set_text (CLUTTER_TEXT (plugin->lock_password), "");

  g_subprocess_communicate_async (plugin->lock_auth_proc,
                                  stdin_bytes,
                                  NULL,
                                  ooze_lock_auth_finished,
                                  plugin);
  g_bytes_unref (stdin_bytes);
}

static gboolean
ooze_lock_on_unlock_clicked (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             gpointer      user_data)
{
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  ooze_lock_try_unlock (OOZE_PLUGIN (user_data));
  return CLUTTER_EVENT_STOP;
}

static void
ooze_lock_on_password_activate (ClutterText *text G_GNUC_UNUSED,
                                gpointer     user_data)
{
  ooze_lock_try_unlock (OOZE_PLUGIN (user_data));
}

static gboolean
ooze_lock_on_overlay_key (ClutterActor *actor G_GNUC_UNUSED,
                          ClutterEvent *event,
                          gpointer      user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  guint key;

  key = clutter_event_get_key_symbol (event);

  /* Escape must not unlock. Swallow so the rest of the shell ignores it. */
  if (key == CLUTTER_KEY_Escape)
    return CLUTTER_EVENT_STOP;

  if (key == CLUTTER_KEY_Return || key == CLUTTER_KEY_KP_Enter)
    {
      ooze_lock_try_unlock (plugin);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
ooze_lock_on_overlay_button (ClutterActor *actor G_GNUC_UNUSED,
                             ClutterEvent *event,
                             gpointer      user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (clutter_event_type (event) == CLUTTER_BUTTON_PRESS &&
      plugin->lock_password)
    clutter_actor_grab_key_focus (plugin->lock_password);

  return CLUTTER_EVENT_STOP;
}

static void
ooze_lock_build_ui (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;
  ClutterActor *card;
  ClutterActor *entry_bg;
  ClutterActor *btn;
  ClutterActor *user_label;
  g_autoptr (ClutterContent) wallpaper = NULL;
  g_autoptr (ClutterContent) card_content = NULL;
  g_autoptr (ClutterContent) entry_content = NULL;
  g_autoptr (ClutterContent) btn_content = NULL;
  CoglColor scrub;
  CoglColor text_color;
  int stage_w = 0;
  int stage_h = 0;
  gfloat card_x;
  gfloat card_y;
  const char *user;
  gboolean dark = ooze_theme_is_dark (NULL);
  gfloat text_r = dark ? 0.95f : 0.12f;
  gfloat text_g = dark ? 0.95f : 0.13f;
  gfloat text_b = dark ? 0.97f : 0.16f;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));
  meta_display_get_size (display, &stage_w, &stage_h);

  cogl_color_init_from_4f (&scrub, 0.0f, 0.0f, 0.0f, dark ? 0.42f : 0.28f);
  cogl_color_init_from_4f (&text_color, text_r, text_g, text_b, 1.0f);

  plugin->lock_overlay = clutter_actor_new ();
  clutter_actor_set_size (plugin->lock_overlay,
                          (gfloat) stage_w,
                          (gfloat) stage_h);
  clutter_actor_set_position (plugin->lock_overlay, 0.0f, 0.0f);
  clutter_actor_set_reactive (plugin->lock_overlay, TRUE);

  wallpaper = ooze_aqua_wallpaper_content (plugin->lock_overlay, stage_w, stage_h);
  if (wallpaper)
    {
      clutter_actor_set_content (plugin->lock_overlay,
                                 g_steal_pointer (&wallpaper));
    }
  else
    {
      CoglColor fallback;

      cogl_color_init_from_4f (&fallback, 18.0f / 255.0f, 20.0f / 255.0f,
                               28.0f / 255.0f, 1.0f);
      clutter_actor_set_background_color (plugin->lock_overlay, &fallback);
    }

  {
    ClutterActor *dim = clutter_actor_new ();

    clutter_actor_set_size (dim, (gfloat) stage_w, (gfloat) stage_h);
    clutter_actor_set_background_color (dim, &scrub);
    clutter_actor_set_reactive (dim, FALSE);
    clutter_actor_add_child (plugin->lock_overlay, dim);
  }

  clutter_actor_add_child (stage, plugin->lock_overlay);
  clutter_actor_set_child_above_sibling (stage, plugin->lock_overlay, NULL);

  g_signal_connect (plugin->lock_overlay, "key-press-event",
                    G_CALLBACK (ooze_lock_on_overlay_key), plugin);
  g_signal_connect (plugin->lock_overlay, "button-press-event",
                    G_CALLBACK (ooze_lock_on_overlay_button), plugin);

  plugin->lock_clock_label = lock_make_label (plugin->lock_overlay,
                                              OOZE_LOCK_CLOCK_FONT,
                                              "0:00",
                                              text_r, text_g, text_b);
  clutter_actor_add_child (plugin->lock_overlay, plugin->lock_clock_label);
  ooze_lock_update_clock (plugin);

  card = clutter_actor_new ();
  clutter_actor_set_size (card, OOZE_LOCK_CARD_W, OOZE_LOCK_CARD_H);
  card_content = ooze_aqua_squircle_panel_content (card,
                                                   (int) OOZE_LOCK_CARD_W,
                                                   (int) OOZE_LOCK_CARD_H,
                                                   FALSE);
  if (card_content)
    clutter_actor_set_content (card, g_steal_pointer (&card_content));
  clutter_actor_set_reactive (card, TRUE);
  clutter_actor_add_child (plugin->lock_overlay, card);
  plugin->lock_card = card;

  user = g_get_real_name ();
  if (!user || !user[0] || g_strcmp0 (user, "Unknown") == 0)
    user = g_get_user_name ();
  user_label = lock_make_label (card, OOZE_LOCK_USER_FONT, user,
                                text_r, text_g, text_b);
  clutter_actor_add_child (card, user_label);
  plugin->lock_user_label = user_label;

  entry_bg = clutter_actor_new ();
  clutter_actor_set_size (entry_bg, OOZE_LOCK_ENTRY_W, OOZE_LOCK_ENTRY_H);
  entry_content = ooze_aqua_squircle_panel_content (entry_bg,
                                                    (int) OOZE_LOCK_ENTRY_W,
                                                    (int) OOZE_LOCK_ENTRY_H,
                                                    TRUE);
  if (entry_content)
    clutter_actor_set_content (entry_bg, g_steal_pointer (&entry_content));
  clutter_actor_set_reactive (entry_bg, TRUE);
  clutter_actor_add_child (card, entry_bg);
  plugin->lock_entry_box = entry_bg;

  plugin->lock_password = clutter_text_new ();
  clutter_text_set_editable (CLUTTER_TEXT (plugin->lock_password), TRUE);
  clutter_text_set_activatable (CLUTTER_TEXT (plugin->lock_password), TRUE);
  clutter_text_set_password_char (CLUTTER_TEXT (plugin->lock_password), 0x2022);
  clutter_text_set_single_line_mode (CLUTTER_TEXT (plugin->lock_password), TRUE);
  clutter_text_set_font_name (CLUTTER_TEXT (plugin->lock_password), OOZE_UI_FONT);
  clutter_text_set_color (CLUTTER_TEXT (plugin->lock_password), &text_color);
  clutter_text_set_cursor_color (CLUTTER_TEXT (plugin->lock_password), &text_color);
  clutter_actor_set_reactive (plugin->lock_password, TRUE);
  clutter_actor_set_size (plugin->lock_password,
                          OOZE_LOCK_ENTRY_W - 24.0f,
                          OOZE_LOCK_ENTRY_H - 10.0f);
  clutter_actor_add_child (entry_bg, plugin->lock_password);
  clutter_actor_set_position (plugin->lock_password, 12.0f, 5.0f);
  g_signal_connect (plugin->lock_password, "activate",
                    G_CALLBACK (ooze_lock_on_password_activate), plugin);

  btn = clutter_actor_new ();
  clutter_actor_set_size (btn, OOZE_LOCK_BTN_W, OOZE_LOCK_BTN_H);
  btn_content = ooze_aqua_kit_button_content (btn, "Unlock",
                                              (int) OOZE_LOCK_BTN_W,
                                              (int) OOZE_LOCK_BTN_H);
  if (btn_content)
    clutter_actor_set_content (btn, g_steal_pointer (&btn_content));
  clutter_actor_set_reactive (btn, TRUE);
  clutter_actor_add_child (card, btn);
  plugin->lock_unlock_btn = btn;
  g_signal_connect (btn, "button-press-event",
                    G_CALLBACK (ooze_lock_on_unlock_clicked), plugin);

  plugin->lock_status_label = lock_make_label (card, OOZE_LOCK_STATUS_FONT, "",
                                               0.92f, 0.45f, 0.38f);
  clutter_actor_add_child (card, plugin->lock_status_label);

  card_x = ((gfloat) stage_w - OOZE_LOCK_CARD_W) / 2.0f;
  card_y = ((gfloat) stage_h - OOZE_LOCK_CARD_H) / 2.0f + 48.0f;
  clutter_actor_set_position (card, card_x, card_y);
  clutter_actor_set_position (user_label,
                              (OOZE_LOCK_CARD_W - clutter_actor_get_width (user_label)) / 2.0f,
                              32.0f);
  clutter_actor_set_position (entry_bg,
                              (OOZE_LOCK_CARD_W - OOZE_LOCK_ENTRY_W) / 2.0f,
                              86.0f);
  clutter_actor_set_position (btn,
                              (OOZE_LOCK_CARD_W - OOZE_LOCK_BTN_W) / 2.0f,
                              148.0f);
  clutter_actor_set_position (plugin->lock_status_label,
                              (OOZE_LOCK_CARD_W -
                               clutter_actor_get_width (plugin->lock_status_label)) / 2.0f,
                              200.0f);
  clutter_actor_set_position (plugin->lock_clock_label,
                              ((gfloat) stage_w -
                               clutter_actor_get_width (plugin->lock_clock_label)) / 2.0f,
                              card_y - 100.0f);

  if (!plugin->lock_clock_timer)
    plugin->lock_clock_timer =
      g_timeout_add_seconds (30, ooze_lock_on_clock_tick, plugin);
}

static void
ooze_lock_destroy_ui (OozePlugin *plugin)
{
  if (plugin->lock_grab)
    {
      clutter_grab_dismiss (plugin->lock_grab);
      plugin->lock_grab = NULL;
    }

  if (plugin->lock_clock_timer)
    {
      g_source_remove (plugin->lock_clock_timer);
      plugin->lock_clock_timer = 0;
    }

  g_clear_pointer (&plugin->lock_overlay, clutter_actor_destroy);
  plugin->lock_card = NULL;
  plugin->lock_clock_label = NULL;
  plugin->lock_user_label = NULL;
  plugin->lock_entry_box = NULL;
  plugin->lock_password = NULL;
  plugin->lock_unlock_btn = NULL;
  plugin->lock_status_label = NULL;
}

static void
ooze_lock_dismiss (OozePlugin *plugin, gboolean authenticated)
{
  if (!plugin->locked)
    return;

  plugin->locked = FALSE;

  if (plugin->lock_auth_proc)
    {
      g_subprocess_force_exit (plugin->lock_auth_proc);
      g_clear_object (&plugin->lock_auth_proc);
    }

  ooze_lock_destroy_ui (plugin);

  if (authenticated)
    {
      ooze_lock_set_locked_hint (FALSE);
      ooze_lock_session_call ("Unlock", NULL);
    }

  ooze_lock_sync_idle_watch (plugin);
  ooze_screensaver_rearm (plugin);
}

void
ooze_lock_request (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  ClutterActor *stage;

  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  if (plugin->shutting_down || plugin->locked)
    return;

  plugin->locked = TRUE;
  ooze_screensaver_dismiss (plugin);

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    ooze_aqua_menu_close (plugin->menu_popup);

  ooze_lock_build_ui (plugin);

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  backend = meta_context_get_backend (meta_display_get_context (display));
  stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

  plugin->lock_grab = clutter_stage_grab (CLUTTER_STAGE (stage),
                                          plugin->lock_overlay);
  if (plugin->lock_password)
    clutter_actor_grab_key_focus (plugin->lock_password);

  ooze_lock_set_locked_hint (TRUE);

  /* Tell logind the session wants Lock (signals may re-enter: idempotent). */
  ooze_lock_session_call ("Lock", NULL);
}

gboolean
ooze_lock_is_locked (OozePlugin *plugin)
{
  return plugin && plugin->locked;
}

static void
ooze_lock_on_idle (MetaIdleMonitor *monitor G_GNUC_UNUSED,
                   guint            watch_id G_GNUC_UNUSED,
                   gpointer         user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down && plugin->lock_enabled)
    ooze_lock_request (plugin);
}

static void
ooze_lock_sync_idle_watch (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;
  guint delay_sec = 0;
  guint lock_delay = 0;
  guint64 interval_ms;

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (!display)
    return;

  backend = meta_context_get_backend (meta_display_get_context (display));
  monitor = meta_backend_get_core_idle_monitor (backend);
  if (!monitor)
    return;

  if (plugin->lock_idle_watch_id)
    {
      meta_idle_monitor_remove_watch (monitor, plugin->lock_idle_watch_id);
      plugin->lock_idle_watch_id = 0;
    }

  if (plugin->locked)
    return;

  if (plugin->session_settings)
    delay_sec = g_settings_get_uint (plugin->session_settings, "idle-delay");
  if (plugin->screensaver_settings)
    {
      plugin->lock_enabled =
        g_settings_get_boolean (plugin->screensaver_settings, "lock-enabled");
      lock_delay =
        g_settings_get_uint (plugin->screensaver_settings, "lock-delay");
    }
  else
    {
      plugin->lock_enabled = TRUE;
    }

  if (!plugin->lock_enabled || delay_sec == 0)
    return;

  interval_ms = ((guint64) delay_sec + (guint64) lock_delay) * 1000ull;
  plugin->lock_idle_watch_id =
    meta_idle_monitor_add_idle_watch (monitor,
                                      interval_ms,
                                      ooze_lock_on_idle,
                                      plugin,
                                      NULL);
}

static void
ooze_lock_on_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                               const char  *key G_GNUC_UNUSED,
                               gpointer     user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    ooze_lock_sync_idle_watch (plugin);
}

static void
ooze_lock_on_logind_lock_signal (GDBusConnection *connection G_GNUC_UNUSED,
                                 const char      *sender G_GNUC_UNUSED,
                                 const char      *object_path G_GNUC_UNUSED,
                                 const char      *interface G_GNUC_UNUSED,
                                 const char      *signal_name,
                                 GVariant        *parameters G_GNUC_UNUSED,
                                 gpointer         user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down &&
      g_strcmp0 (signal_name, "Lock") == 0)
    ooze_lock_request (plugin);
  /* Ignore Unlock from logind — only PAM unlocks the overlay. */
}

static void
ooze_lock_bus_got (GObject      *source G_GNUC_UNUSED,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  g_autoptr (GError) error = NULL;
  GDBusConnection *conn;

  conn = g_bus_get_finish (res, &error);
  if (!conn)
    {
      g_warning ("Ooze lock: cannot watch logind Lock: %s", error->message);
      return;
    }

  if (plugin->shutting_down)
    {
      g_object_unref (conn);
      return;
    }

  plugin->lock_logind_conn = conn;
  plugin->lock_logind_sub_id =
    g_dbus_connection_signal_subscribe (conn,
                                        "org.freedesktop.login1",
                                        "org.freedesktop.login1.Session",
                                        "Lock",
                                        NULL,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        ooze_lock_on_logind_lock_signal,
                                        plugin,
                                        NULL);
}

void
ooze_lock_init (OozePlugin *plugin)
{
  g_return_if_fail (OOZE_IS_PLUGIN (plugin));

  plugin->locked = FALSE;
  plugin->lock_enabled = TRUE;
  plugin->lock_idle_watch_id = 0;
  plugin->lock_logind_sub_id = 0;
  plugin->lock_logind_conn = NULL;

  plugin->session_settings = g_settings_new ("org.gnome.desktop.session");
  plugin->screensaver_settings = g_settings_new ("org.gnome.desktop.screensaver");

  g_signal_connect (plugin->session_settings, "changed::idle-delay",
                    G_CALLBACK (ooze_lock_on_settings_changed), plugin);
  g_signal_connect (plugin->screensaver_settings, "changed::lock-enabled",
                    G_CALLBACK (ooze_lock_on_settings_changed), plugin);
  g_signal_connect (plugin->screensaver_settings, "changed::lock-delay",
                    G_CALLBACK (ooze_lock_on_settings_changed), plugin);

  ooze_lock_sync_idle_watch (plugin);
  g_bus_get (G_BUS_TYPE_SYSTEM, NULL, ooze_lock_bus_got, plugin);
}

void
ooze_lock_dispose (OozePlugin *plugin)
{
  MetaDisplay *display;
  MetaBackend *backend;
  MetaIdleMonitor *monitor;

  if (!plugin)
    return;

  if (plugin->lock_logind_conn && plugin->lock_logind_sub_id)
    {
      g_dbus_connection_signal_unsubscribe (plugin->lock_logind_conn,
                                            plugin->lock_logind_sub_id);
      plugin->lock_logind_sub_id = 0;
    }
  g_clear_object (&plugin->lock_logind_conn);

  display = meta_plugin_get_display (META_PLUGIN (plugin));
  if (display && plugin->lock_idle_watch_id)
    {
      backend = meta_context_get_backend (meta_display_get_context (display));
      monitor = meta_backend_get_core_idle_monitor (backend);
      if (monitor)
        meta_idle_monitor_remove_watch (monitor, plugin->lock_idle_watch_id);
      plugin->lock_idle_watch_id = 0;
    }

  if (plugin->session_settings)
    g_signal_handlers_disconnect_by_data (plugin->session_settings, plugin);
  if (plugin->screensaver_settings)
    g_signal_handlers_disconnect_by_data (plugin->screensaver_settings, plugin);
  g_clear_object (&plugin->session_settings);
  g_clear_object (&plugin->screensaver_settings);

  if (plugin->lock_auth_proc)
    {
      g_subprocess_force_exit (plugin->lock_auth_proc);
      g_clear_object (&plugin->lock_auth_proc);
    }

  plugin->locked = FALSE;
  ooze_lock_destroy_ui (plugin);
}
