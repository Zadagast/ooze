#include "portal-backend.h"

#include "portal-dbus-generated.h"
#include "mutter-screencast-generated.h"
#include "ooze-display-config.h"
#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-surface.h"

#include <adwaita.h>
#include <gtk/gtk.h>

#define MUTTER_BUS "org.gnome.Mutter.ScreenCast"
#define MUTTER_PATH "/org/gnome/Mutter/ScreenCast"
#define MUTTER_IFACE "org.gnome.Mutter.ScreenCast"

#define RESPONSE_SUCCESS 0
#define RESPONSE_CANCELLED 1
#define RESPONSE_FAILED 2
#define SOURCE_MONITOR 1u
#define CURSOR_HIDDEN 1u
#define CURSOR_EMBEDDED 2u
#define CURSOR_METADATA 4u

typedef struct _PortalState PortalState;
typedef struct _PortalSession PortalSession;
typedef struct _PortalRequest PortalRequest;
typedef struct _Picker Picker;
typedef struct _CaptureOperation CaptureOperation;
typedef void (*RequestCompleteFunc) (PortalRequest *, guint, GVariant *);

typedef struct
{
  char *connector;
  char *name;
  int x;
  int y;
  int width;
  int height;
} PickerMonitor;

struct _PortalRequest
{
  grefcount refs;
  PortalState *state;
  char *path;
  guint registration_id;
  OozePortalRequest *skeleton;
  GDBusMethodInvocation *invocation;
  RequestCompleteFunc complete;
  gboolean responded;
  gboolean in_state;
};

struct _PortalSession
{
  grefcount refs;
  PortalState *state;
  char *path;
  guint registration_id;
  OozePortalSession *skeleton;
  GPtrArray *monitors;
  GPtrArray *streams;
  PortalRequest *start_request;
  GVariantBuilder stream_builder;
  guint streams_ready;
  GDBusProxy *mutter_session;
  guint cursor_mode;
  gboolean multiple;
  gboolean closed;
  gboolean in_state;
  gboolean stop_sent;
  gboolean mutter_started;
  Picker *picker;
  CaptureOperation *capture;
};

struct _PortalState
{
  grefcount refs;
  GDBusConnection *connection;
  OozePortalScreenCast *screen_cast;
  guint screen_cast_registration_id;
  GHashTable *requests;
  GHashTable *sessions;
  gboolean stopping;
};

struct _Picker
{
  PortalRequest *request;
  PortalSession *session;
  GtkWindow *window;
  GPtrArray *monitors;
  GPtrArray *selected;
  GtkWidget *list;
  gboolean finished;
};

struct _CaptureOperation
{
  grefcount refs;
  PortalSession *session;
  PortalRequest *request;
  guint monitor_index;
  gboolean finished;
};

void
ooze_portal_backend_ensure_gtk (void)
{
  static gsize initialized;

  if (g_once_init_enter (&initialized))
    {
      gtk_init ();
      g_once_init_leave (&initialized, 1);
    }
}

static PortalState *portal_state_ref (PortalState *state);
static void portal_state_unref (PortalState *state);
static PortalRequest *request_ref (PortalRequest *request);
static void request_unref (PortalRequest *request);
static PortalSession *session_ref (PortalSession *session);
static void session_unref (PortalSession *session);
static void request_respond (PortalRequest *request,
                             guint          response,
                             GVariant      *results);
static void session_close (PortalSession *session,
                           guint          pending_response);
static void picker_finish (Picker *picker,
                           gboolean accepted);
static void picker_cancel (Picker *picker,
                           guint response);
static void session_clear_start_request (PortalSession *session,
                                         guint response);
static void capture_abort (CaptureOperation *capture);
static CaptureOperation *capture_ref (CaptureOperation *capture);
static void capture_unref (CaptureOperation *capture);
static void capture_fail (CaptureOperation *capture,
                          char             *message);
static void on_stream_signal (GDBusProxy *proxy,
                              const char  *sender_name,
                              const char  *signal_name,
                              GVariant    *parameters,
                              gpointer     user_data);
static void start_mutter (PortalRequest *request, PortalSession *session);
static GVariant *empty_results (void);

static void
picker_monitor_free (PickerMonitor *monitor)
{
  g_free (monitor->connector);
  g_free (monitor->name);
  g_free (monitor);
}

static void
portal_state_free (PortalState *state)
{
  if (!state)
    return;

  if (state->screen_cast)
    g_dbus_interface_skeleton_unexport (
      G_DBUS_INTERFACE_SKELETON (state->screen_cast));
  g_clear_object (&state->screen_cast);
  g_clear_pointer (&state->requests, g_hash_table_unref);
  g_clear_pointer (&state->sessions, g_hash_table_unref);
  g_clear_object (&state->connection);
  g_free (state);
}

static PortalState *
portal_state_ref (PortalState *state)
{
  if (state)
    g_ref_count_inc (&state->refs);
  return state;
}

static void
portal_state_unref (PortalState *state)
{
  if (state && g_ref_count_dec (&state->refs))
    portal_state_free (state);
}

static void
request_free (PortalRequest *request)
{
  if (!request)
    return;

  g_dbus_interface_skeleton_unexport (
    G_DBUS_INTERFACE_SKELETON (request->skeleton));
  g_clear_object (&request->skeleton);
  g_clear_object (&request->invocation);
  portal_state_unref (request->state);
  g_free (request->path);
  g_free (request);
}

static PortalRequest *
request_ref (PortalRequest *request)
{
  if (request)
    g_ref_count_inc (&request->refs);
  return request;
}

static void
request_unref (PortalRequest *request)
{
  if (request && g_ref_count_dec (&request->refs))
    request_free (request);
}

static void
session_disconnect_streams (PortalSession *session)
{
  guint i;

  if (!session || !session->streams)
    return;

  for (i = 0; i < session->streams->len; i++)
    {
      GDBusProxy *stream = session->streams->pdata[i];

      g_signal_handlers_disconnect_by_func (stream, on_stream_signal, session);
    }
}

static void
stop_mutter_cb (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  GDBusProxy *proxy = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!reply && error &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("SC Mutter Stop error=%s", error->message);
  g_clear_object (&proxy);
}

static void
session_stop_mutter (PortalSession *session)
{
  GDBusProxy *mutter_session;

  if (!session || session->stop_sent)
    return;

  session->stop_sent = TRUE;
  session->mutter_started = FALSE;
  session_disconnect_streams (session);
  mutter_session = g_steal_pointer (&session->mutter_session);
  if (!mutter_session)
    return;

  g_dbus_proxy_call (mutter_session, "Stop", NULL,
                     G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                     stop_mutter_cb, g_object_ref (mutter_session));
  g_clear_object (&mutter_session);
}

static void
capture_free (CaptureOperation *capture)
{
  if (!capture)
    return;
  if (capture->session && capture->session->capture == capture)
    capture->session->capture = NULL;
  request_unref (capture->request);
  session_unref (capture->session);
  g_free (capture);
}

static CaptureOperation *
capture_ref (CaptureOperation *capture)
{
  if (capture)
    g_ref_count_inc (&capture->refs);
  return capture;
}

static void
capture_unref (CaptureOperation *capture)
{
  if (capture && g_ref_count_dec (&capture->refs))
    capture_free (capture);
}

static void
capture_abort (CaptureOperation *capture)
{
  if (!capture || capture->finished)
    return;

  capture->finished = TRUE;
  if (capture->session && capture->session->capture == capture)
    capture->session->capture = NULL;
  capture_unref (capture);
}

static gboolean
capture_is_live (CaptureOperation *capture)
{
  return capture &&
         !capture->finished &&
         capture->session &&
         !capture->session->closed &&
         capture->session->start_request == capture->request;
}

static void
capture_fail (CaptureOperation *capture,
              char             *message)
{
  if (!capture || capture->finished)
    return;

  if (message)
    g_warning ("%s", message);
  g_free (message);
  capture->finished = TRUE;
  if (capture->session->start_request == capture->request)
    session_clear_start_request (capture->session, RESPONSE_FAILED);
  if (capture->session->capture == capture)
    capture->session->capture = NULL;
  capture_unref (capture);
}

static void
session_free (PortalSession *session)
{
  if (!session)
    return;

  session_stop_mutter (session);
  g_clear_object (&session->skeleton);
  g_clear_pointer (&session->monitors, g_ptr_array_unref);
  g_clear_pointer (&session->streams, g_ptr_array_unref);
  if (session->start_request)
    request_unref (g_steal_pointer (&session->start_request));
  portal_state_unref (session->state);
  g_free (session->path);
  g_free (session);
}

static PortalSession *
session_ref (PortalSession *session)
{
  if (session)
    g_ref_count_inc (&session->refs);
  return session;
}

static void
session_unref (PortalSession *session)
{
  if (session && g_ref_count_dec (&session->refs))
    session_free (session);
}

static void
request_remove_from_state (PortalRequest *request)
{
  if (!request || !request->in_state)
    return;

  request->in_state = FALSE;
  g_hash_table_remove (request->state->requests, request->path);
}

static void
session_remove_from_state (PortalSession *session)
{
  if (!session || !session->in_state)
    return;

  session->in_state = FALSE;
  g_hash_table_remove (session->state->sessions, session->path);
}

static void
request_respond (PortalRequest *request,
                 guint          response,
                 GVariant      *results)
{
  if (!request || request->responded)
    {
      if (results)
        {
          g_variant_ref_sink (results);
          g_variant_unref (results);
        }
      return;
    }

  request->responded = TRUE;
  if (request->complete && request->invocation)
    {
      request->complete (request, response, results);
    }
  else
    {
      if (results)
        {
          g_variant_ref_sink (results);
          g_variant_unref (results);
        }
      g_clear_object (&request->invocation);
    }

  g_dbus_interface_skeleton_unexport (
    G_DBUS_INTERFACE_SKELETON (request->skeleton));
  request->registration_id = 0;
  request_remove_from_state (request);
}

static void
complete_request_method (PortalRequest *request,
                         guint          response,
                         GVariant      *results)
{
  request_respond (request, response, results);
}

static void
complete_dbus_request (PortalRequest *request,
                       guint          response,
                       GVariant      *results)
{
  g_message ("SC CreateSession response=%u", response);
  ooze_portal_screen_cast_complete_create_session (
    request->state->screen_cast, request->invocation, response, results);
  g_clear_object (&request->invocation);
}

static void
complete_dbus_select_sources (PortalRequest *request,
                              guint          response,
                              GVariant      *results)
{
  g_message ("SC SelectSources response=%u", response);
  ooze_portal_screen_cast_complete_select_sources (
    request->state->screen_cast, request->invocation, response, results);
  g_clear_object (&request->invocation);
}

static void
complete_dbus_start (PortalRequest *request,
                     guint          response,
                     GVariant      *results)
{
  g_message ("SC Start response=%u", response);
  ooze_portal_screen_cast_complete_start (
    request->state->screen_cast, request->invocation, response, results);
  g_clear_object (&request->invocation);
}

static gboolean
on_request_close (OozePortalRequest *object,
                  GDBusMethodInvocation                      *invocation,
                  gpointer                                    user_data)
{
  PortalRequest *request = user_data;
  GHashTableIter iter;
  gpointer value;

  request_ref (request);
  g_hash_table_iter_init (&iter, request->state->sessions);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      PortalSession *session = value;
      if (session->start_request == request)
        {
          if (session->picker)
            picker_cancel (session->picker, RESPONSE_CANCELLED);
          else
            {
              capture_abort (session->capture);
              session_clear_start_request (session, RESPONSE_CANCELLED);
            }
          break;
        }
    }
  request_respond (request, RESPONSE_CANCELLED, empty_results ());
  g_dbus_method_invocation_return_value (invocation, NULL);
  request_unref (request);
  (void) object;
  return TRUE;
}

static PortalRequest *
request_new (PortalState *state,
             const char  *path,
             GDBusMethodInvocation *invocation,
             RequestCompleteFunc complete)
{
  PortalRequest *request = g_new0 (PortalRequest, 1);

  g_ref_count_init (&request->refs);
  request->state = portal_state_ref (state);
  request->path = g_strdup (path);
  request->invocation = g_object_ref (invocation);
  request->complete = complete;
  request->skeleton = ooze_portal_request_skeleton_new ();
  g_signal_connect (request->skeleton, "handle-close",
                    G_CALLBACK (on_request_close), request);
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (request->skeleton),
                                    state->connection, path, NULL);
  request->in_state = TRUE;
  g_hash_table_insert (state->requests, g_strdup (path), request_ref (request));
  return request;
}

static gboolean
on_session_close (OozePortalSession *object,
                  GDBusMethodInvocation                       *invocation,
                  gpointer                                     user_data)
{
  PortalSession *session = user_data;

  session_ref (session);
  session_close (session, RESPONSE_FAILED);
  g_dbus_method_invocation_return_value (invocation, NULL);
  session_unref (session);
  (void) object;
  return TRUE;
}

static void
session_clear_start_request (PortalSession *session,
                             guint          response)
{
  PortalRequest *request;

  if (!session || !session->start_request)
    return;

  request = g_steal_pointer (&session->start_request);
  request_respond (request, response, empty_results ());
  request_unref (request);
}

static void
session_close (PortalSession *session,
               guint          pending_response)
{
  if (!session || session->closed)
    return;

  session->closed = TRUE;
  capture_abort (session->capture);
  if (session->picker)
    picker_cancel (session->picker, pending_response);
  session_clear_start_request (session, pending_response);
  session_stop_mutter (session);
  if (session->skeleton)
    ooze_portal_session_emit_closed (session->skeleton);
  g_dbus_interface_skeleton_unexport (
    G_DBUS_INTERFACE_SKELETON (session->skeleton));
  session_remove_from_state (session);
}

static PortalSession *
session_new (PortalState *state,
             const char  *path)
{
  PortalSession *session = g_new0 (PortalSession, 1);

  g_ref_count_init (&session->refs);
  session->state = portal_state_ref (state);
  session->path = g_strdup (path);
  session->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) picker_monitor_free);
  session->streams = g_ptr_array_new_with_free_func (g_object_unref);
  session->skeleton = ooze_portal_session_skeleton_new ();
  ooze_portal_session_set_version (session->skeleton, 1);
  g_signal_connect (session->skeleton, "handle-close",
                    G_CALLBACK (on_session_close), session);
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (session->skeleton),
                                    state->connection, path, NULL);
  session->in_state = TRUE;
  g_hash_table_insert (state->sessions, g_strdup (path), session_ref (session));
  return session;
}

static GVariant *
empty_results (void)
{
  return g_variant_new ("a{sv}", NULL);
}

static void
complete_create (PortalRequest *request,
                 guint          response,
                 const char    *session_id)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (session_id)
    g_variant_builder_add (&builder, "{sv}", "session_id",
                           g_variant_new_string (session_id));
  complete_request_method (request, response, g_variant_builder_end (&builder));
}

static gboolean
on_create_session (OozePortalScreenCast *object,
                   GDBusMethodInvocation                         *invocation,
                   const char                                    *handle,
                   const char                                    *session_handle,
                   const char                                    *app_id,
                   GVariant                                      *options,
                   gpointer                                       user_data)
{
  PortalState *state = user_data;
  PortalRequest *request;
  PortalSession *session;

  g_message ("SC CreateSession app_id=%s", app_id ? app_id : "");
  request = request_new (state, handle, invocation, complete_dbus_request);
  session = session_new (state, session_handle);
  complete_create (request, RESPONSE_SUCCESS, session->path);
  request_unref (request);
  session_unref (session);
  (void) object;
  (void) app_id;
  (void) options;
  return TRUE;
}

static gboolean
on_select_sources (OozePortalScreenCast *object,
                   GDBusMethodInvocation                         *invocation,
                   const char                                    *handle,
                   const char                                    *session_handle,
                   const char                                    *app_id,
                   GVariant                                      *options,
                   gpointer                                       user_data)
{
  PortalState *state = user_data;
  PortalSession *session = session_ref (
    g_hash_table_lookup (state->sessions, session_handle));
  PortalRequest *request;
  guint types = SOURCE_MONITOR;
  gboolean multiple = FALSE;
  guint cursor_mode = CURSOR_HIDDEN;

  if (!session)
    {
      g_warning ("SC SelectSources unknown-session");
      g_dbus_method_invocation_return_dbus_error (invocation,
                                                  "org.freedesktop.impl.portal.Error.NotFound",
                                                  "Unknown screen cast session");
      return TRUE;
    }
  request = request_new (state, handle, invocation, complete_dbus_select_sources);
  g_variant_lookup (options, "types", "u", &types);
  g_variant_lookup (options, "multiple", "b", &multiple);
  g_variant_lookup (options, "cursor_mode", "u", &cursor_mode);
  session->multiple = multiple;
  session->cursor_mode = cursor_mode;
  g_message ("SC SelectSources types=%u multiple=%d cursor_mode=%u decision=accept-monitor",
             types, multiple, cursor_mode);
  /* Monitor capture is the only implemented source. Keep accepting requests
   * with other type bits so the picker can offer the available monitor. */
  complete_request_method (request, RESPONSE_SUCCESS, empty_results ());
  request_unref (request);
  session_unref (session);
  (void) object;
  (void) app_id;
  return TRUE;
}

static void
picker_free (Picker *picker)
{
  if (!picker)
    return;
  if (picker->session && picker->session->picker == picker)
    picker->session->picker = NULL;
  g_clear_pointer (&picker->monitors, g_ptr_array_unref);
  g_clear_pointer (&picker->selected, g_ptr_array_unref);
  request_unref (picker->request);
  session_unref (picker->session);
  g_free (picker);
}

static void
picker_cancel (Picker *picker,
               guint    response)
{
  GtkWindow *window;

  if (!picker || picker->finished)
    return;

  picker->finished = TRUE;
  window = g_steal_pointer (&picker->window);
  if (window)
    gtk_window_destroy (window);
  session_clear_start_request (picker->session, response);
  picker_free (picker);
}

static void
picker_finish (Picker *picker,
               gboolean  accepted)
{
  PortalSession *session;
  PortalRequest *request;
  GtkWindow *window;
  guint i;

  if (!picker || picker->finished)
    return;
  if (!accepted)
    {
      g_message ("SC picker cancel");
      picker_cancel (picker, RESPONSE_CANCELLED);
      return;
    }

  session = picker->session;
  request = picker->request;
  window = g_steal_pointer (&picker->window);
  picker->finished = TRUE;
  g_ptr_array_set_size (session->monitors, 0);
  for (i = 0; i < picker->selected->len; i++)
    {
      PickerMonitor *source = picker->selected->pdata[i];
      PickerMonitor *copy = g_new0 (PickerMonitor, 1);
      *copy = *source;
      copy->connector = g_strdup (source->connector);
      copy->name = g_strdup (source->name);
      g_ptr_array_add (session->monitors, copy);
    }
  g_message ("SC picker accept monitors=%u", picker->selected->len);
  if (window)
    gtk_window_destroy (window);
  start_mutter (request, session);
  picker_free (picker);
}

static void
on_picker_cancel (GtkButton *button,
                  gpointer   user_data)
{
  picker_finish (user_data, FALSE);
  (void) button;
}

static void
on_picker_accept (GtkButton *button,
                  gpointer   user_data)
{
  Picker *picker = user_data;
  guint i;
  GtkListBoxRow *row;

  for (i = 0; (row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (picker->list), i)); i++)
    {
      GtkWidget *check = gtk_list_box_row_get_child (row);
      if (gtk_check_button_get_active (GTK_CHECK_BUTTON (check)))
        {
          g_ptr_array_add (picker->selected, picker->monitors->pdata[i]);
          if (!picker->session->multiple)
            break;
        }
    }
  if (picker->selected->len == 0)
    return;
  picker_finish (picker, TRUE);
  (void) button;
}

static void
on_picker_close (GtkWindow *window,
                 gpointer   user_data)
{
  Picker *picker = user_data;

  if (picker->window)
    {
      g_message ("SC picker window closed");
      picker_cancel (picker, RESPONSE_CANCELLED);
    }
  (void) window;
}

static void
show_picker (PortalRequest *request,
             PortalSession *session)
{
  OozeDisplayConfig *config = NULL;
  g_autoptr (GError) error = NULL;
  Picker *picker;
  GtkWidget *root;
  GtkWidget *surface;
  GtkWidget *box;
  GtkWidget *buttons;
  GtkWidget *accept;
  GtkWidget *cancel;
  guint i;

  g_message ("SC picker entered");
  ooze_portal_backend_ensure_gtk ();
  if (!ooze_display_config_load (&config, &error))
    {
      g_warning ("SC picker DisplayConfig error=%s",
                 error ? error->message : "unknown");
      complete_request_method (request, RESPONSE_FAILED, empty_results ());
      return;
    }
  g_message ("SC picker monitors=%u", config->monitors->len);
  if (config->monitors->len == 0)
    {
      g_warning ("SC picker no-monitors");
      ooze_display_config_free (config);
      complete_request_method (request, RESPONSE_FAILED, empty_results ());
      return;
    }

  picker = g_new0 (Picker, 1);
  picker->request = request_ref (request);
  picker->session = session_ref (session);
  session->start_request = request_ref (request);
  session->picker = picker;
  picker->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) picker_monitor_free);
  picker->selected = g_ptr_array_new ();
  picker->window = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_title (picker->window, "Share a Monitor");
  gtk_window_set_default_size (picker->window, 460, 360);
  g_signal_connect (picker->window, "close-request",
                    G_CALLBACK (on_picker_close), picker);

  root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (surface, 18);
  gtk_widget_set_margin_end (surface, 18);
  gtk_widget_set_margin_top (surface, 18);
  gtk_widget_set_margin_bottom (surface, 18);
  gtk_box_append (GTK_BOX (root), surface);
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_append (GTK_BOX (surface), box);
  gtk_box_append (GTK_BOX (box), gtk_label_new ("Choose the monitor to share."));
  picker->list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (picker->list), GTK_SELECTION_NONE);
  gtk_widget_set_vexpand (picker->list, TRUE);
  gtk_box_append (GTK_BOX (box), picker->list);

  for (i = 0; i < config->monitors->len; i++)
    {
      OozeDisplayMonitor *monitor = config->monitors->pdata[i];
      PickerMonitor *source = g_new0 (PickerMonitor, 1);
      GtkWidget *check;
      OozeDisplayMode *mode = NULL;
      guint j;

      source->connector = g_strdup (monitor->connector);
      source->name = g_strdup (monitor->display_name);
      source->x = monitor->layout_x;
      source->y = monitor->layout_y;
      for (j = 0; j < monitor->modes->len; j++)
        {
          OozeDisplayMode *candidate = monitor->modes->pdata[j];
          if (g_strcmp0 (candidate->id, monitor->current_mode_id) == 0)
            {
              mode = candidate;
              break;
            }
        }
      if (mode)
        {
          source->width = mode->width;
          source->height = mode->height;
        }
      g_ptr_array_add (picker->monitors, source);
      check = gtk_check_button_new_with_label (source->name);
      gtk_widget_set_margin_top (check, 6);
      gtk_widget_set_margin_bottom (check, 6);
      gtk_list_box_append (GTK_LIST_BOX (picker->list), check);
    }
  ooze_display_config_free (config);

  buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign (buttons, GTK_ALIGN_END);
  cancel = gtk_button_new_with_label ("Cancel");
  accept = gtk_button_new_with_label ("Share");
  gtk_box_append (GTK_BOX (buttons), cancel);
  gtk_box_append (GTK_BOX (buttons), accept);
  gtk_box_append (GTK_BOX (box), buttons);
  g_signal_connect (cancel, "clicked", G_CALLBACK (on_picker_cancel), picker);
  g_signal_connect (accept, "clicked", G_CALLBACK (on_picker_accept), picker);
  gtk_window_set_child (picker->window, root);
  gtk_window_present (picker->window);
  g_message ("SC picker presented");
}

static void
stream_ready (PortalSession *session,
              GDBusProxy    *stream,
              guint          node_id)
{
  PickerMonitor *monitor;
  GVariantBuilder properties;

  if (!session || session->closed ||
      !session->start_request ||
      !session->mutter_started ||
      session->streams_ready >= session->monitors->len)
    return;

  monitor = session->monitors->pdata[session->streams_ready];
  g_variant_builder_init (&properties, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties, "{sv}", "source_type",
                         g_variant_new_uint32 (SOURCE_MONITOR));
  g_variant_builder_add (&properties, "{sv}", "position",
                         g_variant_new ("(ii)", monitor->x, monitor->y));
  g_variant_builder_add (&properties, "{sv}", "size",
                         g_variant_new ("(ii)", monitor->width, monitor->height));
  g_variant_builder_add (&session->stream_builder, "(u@a{sv})", node_id,
                         g_variant_builder_end (&properties));
  session->streams_ready++;
  if (session->streams_ready == session->streams->len && session->start_request)
    {
      GVariantBuilder results;
      GVariant *streams = g_variant_builder_end (&session->stream_builder);
      PortalRequest *request = g_steal_pointer (&session->start_request);

      g_variant_builder_init (&results, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&results, "{sv}", "streams", streams);
      complete_request_method (request, RESPONSE_SUCCESS,
                               g_variant_builder_end (&results));
      request_unref (request);
      capture_abort (session->capture);
    }
  (void) stream;
}

static void
on_stream_signal (GDBusProxy *proxy,
                  const char  *sender_name,
                  const char  *signal_name,
                  GVariant    *parameters,
                  gpointer     user_data)
{
  PortalSession *session = user_data;
  guint node_id;

  if (g_strcmp0 (signal_name, "PipeWireStreamAdded") != 0)
    return;
  g_variant_get (parameters, "(u)", &node_id);
  g_message ("SC PipeWireStreamAdded node_id=%u", node_id);
  stream_ready (session, proxy, node_id);
  (void) sender_name;
}

static void capture_create_cb (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data);
static void capture_session_proxy_cb (GObject      *source,
                                      GAsyncResult *res,
                                      gpointer      user_data);
static void capture_record_cb (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data);
static void capture_stream_proxy_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data);
static void capture_start_cb (GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data);

static void
capture_start_next_record (CaptureOperation *capture)
{
  PortalSession *session = capture->session;
  PickerMonitor *monitor;
  GVariantBuilder record_props;

  if (!capture_is_live (capture))
    {
      capture_abort (capture);
      return;
    }

  if (capture->monitor_index >= session->monitors->len)
    {
      capture_ref (capture);
      g_dbus_proxy_call (session->mutter_session, "Start", NULL,
                         G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                         capture_start_cb, capture);
      return;
    }

  monitor = session->monitors->pdata[capture->monitor_index];
  g_variant_builder_init (&record_props, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&record_props, "{sv}", "cursor-mode",
                         g_variant_new_uint32 (session->cursor_mode == 2 ? 1 :
                                               session->cursor_mode == 4 ? 2 : 0));
  capture_ref (capture);
  g_dbus_proxy_call (session->mutter_session, "RecordMonitor",
                     g_variant_new ("(s@a{sv})",
                                    monitor->connector,
                                    g_variant_builder_end (&record_props)),
                     G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                     capture_record_cb, capture);
}

static void
capture_create_cb (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  CaptureOperation *capture = user_data;
  PortalSession *session = capture->session;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  const char *path;

  if (!capture_is_live (capture))
    {
      g_autoptr (GVariant) discard =
        g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, NULL);
      capture_abort (capture);
      capture_unref (capture);
      return;
    }
  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
  if (!reply)
    {
      capture_fail (capture, g_strdup_printf ("SC Mutter CreateSession error=%s",
                                               error ? error->message : "unknown"));
      capture_unref (capture);
      return;
    }
  g_variant_get (reply, "(&o)", &path);
  g_message ("SC Mutter CreateSession path=%s", path);
  capture_ref (capture);
  g_dbus_proxy_new (session->state->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                    MUTTER_BUS, path,
                    "org.gnome.Mutter.ScreenCast.Session",
                    NULL, capture_session_proxy_cb, capture);
  capture_unref (capture);
}

static void
capture_session_proxy_cb (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  CaptureOperation *capture = user_data;
  PortalSession *session = capture->session;
  g_autoptr (GError) error = NULL;
  GDBusProxy *proxy;

  if (!capture_is_live (capture))
    {
      g_autoptr (GDBusProxy) discard = g_dbus_proxy_new_finish (res, NULL);
      capture_abort (capture);
      capture_unref (capture);
      return;
    }
  proxy = g_dbus_proxy_new_finish (res, &error);
  if (!proxy)
    {
      capture_fail (capture,
                    g_strdup_printf ("SC Mutter session proxy error=%s",
                                     error ? error->message : "unknown"));
      capture_unref (capture);
      return;
    }
  session->mutter_session = proxy;
  capture_start_next_record (capture);
  capture_unref (capture);
  (void) source;
}

static void
capture_record_cb (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  CaptureOperation *capture = user_data;
  PortalSession *session = capture->session;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;
  const char *stream_path;

  if (!capture_is_live (capture))
    {
      g_autoptr (GVariant) discard =
        g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, NULL);
      capture_abort (capture);
      capture_unref (capture);
      return;
    }
  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!reply)
    {
      PickerMonitor *monitor = session->monitors->pdata[capture->monitor_index];
      capture_fail (
        capture,
        g_strdup_printf ("SC Mutter RecordMonitor connector=%s error=%s",
                         monitor->connector,
                         error ? error->message : "unknown"));
      capture_unref (capture);
      return;
    }
  g_variant_get (reply, "(&o)", &stream_path);
  g_message ("SC Mutter RecordMonitor connector=%s path=%s",
             ((PickerMonitor *) session->monitors->pdata[capture->monitor_index])->connector,
             stream_path);
  capture_ref (capture);
  g_dbus_proxy_new (session->state->connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                    MUTTER_BUS, stream_path,
                    "org.gnome.Mutter.ScreenCast.Stream",
                    NULL, capture_stream_proxy_cb, capture);
  capture_unref (capture);
}

static void
capture_stream_proxy_cb (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  CaptureOperation *capture = user_data;
  PortalSession *session = capture->session;
  g_autoptr (GError) error = NULL;
  GDBusProxy *stream;

  if (!capture_is_live (capture))
    {
      g_autoptr (GDBusProxy) discard = g_dbus_proxy_new_finish (res, NULL);
      capture_abort (capture);
      capture_unref (capture);
      return;
    }
  stream = g_dbus_proxy_new_finish (res, &error);
  if (!stream)
    {
      PickerMonitor *monitor = session->monitors->pdata[capture->monitor_index];
      capture_fail (
        capture,
        g_strdup_printf ("SC Mutter stream proxy connector=%s error=%s",
                         monitor->connector,
                         error ? error->message : "unknown"));
      capture_unref (capture);
      return;
    }
  g_signal_connect (stream, "g-signal", G_CALLBACK (on_stream_signal), session);
  g_ptr_array_add (session->streams, stream);
  capture->monitor_index++;
  capture_start_next_record (capture);
  capture_unref (capture);
  (void) source;
}

static void
capture_start_cb (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  CaptureOperation *capture = user_data;
  PortalSession *session = capture->session;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = NULL;

  if (!capture_is_live (capture))
    {
      g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, NULL);
      capture_abort (capture);
      capture_unref (capture);
      return;
    }
  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), res, &error);
  if (!reply)
    {
      capture_fail (capture,
                    g_strdup_printf ("SC Mutter Start error=%s",
                                     error ? error->message : "unknown"));
      capture_unref (capture);
      return;
    }
  session->mutter_started = TRUE;
  g_message ("SC Mutter Start success");
  /* PipeWireStreamAdded is asynchronous. */
  capture_unref (capture);
}

static void
start_mutter (PortalRequest *request,
              PortalSession *session)
{
  CaptureOperation *capture;
  GVariantBuilder props;

  if (session->closed || session->start_request != request)
    return;
  session->streams_ready = 0;
  session->mutter_started = FALSE;
  session_disconnect_streams (session);
  g_ptr_array_set_size (session->streams, 0);
  g_variant_builder_init (&session->stream_builder, G_VARIANT_TYPE ("a(ua{sv})"));
  capture = g_new0 (CaptureOperation, 1);
  g_ref_count_init (&capture->refs);
  capture->session = session_ref (session);
  capture->request = request_ref (request);
  session->capture = capture;
  g_message ("SC Mutter CreateSession begin monitors=%u",
             session->monitors->len);
  g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
  capture_ref (capture);
  g_dbus_connection_call (session->state->connection,
                          MUTTER_BUS, MUTTER_PATH, MUTTER_IFACE,
                          "CreateSession",
                          g_variant_new ("(@a{sv})",
                                         g_variant_builder_end (&props)),
                          G_VARIANT_TYPE ("(o)"),
                          G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
                          capture_create_cb, capture);
}

static gboolean
on_start (OozePortalScreenCast *object,
          GDBusMethodInvocation                         *invocation,
          const char                                    *handle,
          const char                                    *session_handle,
          const char                                    *app_id,
          const char                                    *parent_window,
          GVariant                                      *options,
          gpointer                                       user_data)
{
  PortalState *state = user_data;
  PortalSession *session = session_ref (
    g_hash_table_lookup (state->sessions, session_handle));
  PortalRequest *request;

  g_message ("SC Start app_id=%s", app_id ? app_id : "");
  if (!session)
    {
      g_warning ("SC Start unknown-session");
      g_dbus_method_invocation_return_dbus_error (invocation,
                                                  "org.freedesktop.impl.portal.Error.NotFound",
                                                  "Unknown screen cast session");
      return TRUE;
    }
  if (session->closed || session->start_request)
    {
      g_warning ("SC Start session already has a pending request");
      g_dbus_method_invocation_return_dbus_error (
        invocation,
        "org.freedesktop.impl.portal.Error.Failed",
        "Screen cast session already has a pending Start request");
      session_unref (session);
      return TRUE;
    }
  request = request_new (state, handle, invocation, complete_dbus_start);
  show_picker (request, session);
  request_unref (request);
  session_unref (session);
  (void) object;
  (void) app_id;
  (void) parent_window;
  (void) options;
  return TRUE;
}

OozePortalBackend *
ooze_portal_backend_start (GDBusConnection *connection)
{
  PortalState *state = g_new0 (PortalState, 1);

  g_ref_count_init (&state->refs);
  state->connection = g_object_ref (connection);
  state->requests = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                           (GDestroyNotify) request_unref);
  state->sessions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                           (GDestroyNotify) session_unref);
  state->screen_cast = ooze_portal_screen_cast_skeleton_new ();
  ooze_portal_screen_cast_set_available_source_types (
    state->screen_cast, SOURCE_MONITOR);
  ooze_portal_screen_cast_set_available_cursor_modes (
    state->screen_cast, CURSOR_HIDDEN | CURSOR_EMBEDDED | CURSOR_METADATA);
  ooze_portal_screen_cast_set_version (state->screen_cast, 5);
  g_signal_connect (state->screen_cast, "handle-create-session",
                    G_CALLBACK (on_create_session), state);
  g_signal_connect (state->screen_cast, "handle-select-sources",
                    G_CALLBACK (on_select_sources), state);
  g_signal_connect (state->screen_cast, "handle-start",
                    G_CALLBACK (on_start), state);
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (state->screen_cast),
                                    connection,
                                    "/org/freedesktop/portal/desktop",
                                    NULL);
  return state;
}

void
ooze_portal_backend_stop (OozePortalBackend *backend)
{
  PortalState *state = backend;
  GPtrArray *sessions;
  GPtrArray *requests;
  GHashTableIter iter;
  gpointer value;
  guint i;

  if (!state || state->stopping)
    return;

  state->stopping = TRUE;
  if (state->screen_cast)
    g_dbus_interface_skeleton_unexport (
      G_DBUS_INTERFACE_SKELETON (state->screen_cast));

  sessions = g_ptr_array_new_with_free_func ((GDestroyNotify) session_unref);
  g_hash_table_iter_init (&iter, state->sessions);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    g_ptr_array_add (sessions, session_ref (value));
  for (i = 0; i < sessions->len; i++)
    session_close (sessions->pdata[i], RESPONSE_FAILED);
  g_ptr_array_unref (sessions);

  requests = g_ptr_array_new_with_free_func ((GDestroyNotify) request_unref);
  g_hash_table_iter_init (&iter, state->requests);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    g_ptr_array_add (requests, request_ref (value));
  for (i = 0; i < requests->len; i++)
    request_respond (requests->pdata[i], RESPONSE_FAILED, empty_results ());
  g_ptr_array_unref (requests);

  g_hash_table_remove_all (state->sessions);
  g_hash_table_remove_all (state->requests);
  portal_state_unref (state);
}
