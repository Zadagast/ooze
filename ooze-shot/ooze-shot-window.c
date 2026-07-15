#include "ooze-shot-window.h"

#include "ooze-button.h"
#include "ooze-application.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <gdk/gdk.h>

#define SHOT_BUS_NAME "org.ooze.Shell.Screenshot"
#define SHOT_OBJECT_PATH "/org/ooze/Shell/Screenshot"
#define SHOT_INTERFACE "org.ooze.Shell.Screenshot"

struct _OozeShotWindow
{
  OozeApplicationWindow parent_instance;

  GDBusConnection *session;
  GtkWidget       *mode;
  GtkWidget       *delay;
  GtkWidget       *capture;
  GtkWidget       *preview;
  GtkWidget       *status;
  GtkWidget       *copy;
  GtkWidget       *reveal;
  GtkWidget       *eye;
  GdkTexture      *texture;
  char            *path;
  guint            delay_id;
  gboolean         capture_in_flight;
  gboolean         hidden_for_capture;
};

G_DEFINE_FINAL_TYPE (OozeShotWindow, ooze_shot_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static const char *const capture_icons[] = { "camera-photo", "camera", NULL };
static const char *const copy_icons[] = { "edit-copy", NULL };
static const char *const reveal_icons[] = { "folder-open", "document-open", NULL };
static const char *const eye_icons[] = { "image-x-generic", "image-viewer", NULL };

static void
shot_set_status (OozeShotWindow *self,
                 const char     *message)
{
  gtk_label_set_text (GTK_LABEL (self->status), message);
}

static void
shot_show_window (OozeShotWindow *self)
{
  if (!self->hidden_for_capture)
    return;

  self->hidden_for_capture = FALSE;
  gtk_window_present (GTK_WINDOW (self));
}

static void
shot_capture_failed (OozeShotWindow *self,
                      const char     *message)
{
  self->capture_in_flight = FALSE;
  gtk_widget_set_sensitive (self->capture, TRUE);
  shot_show_window (self);
  shot_set_status (self, message);
}

static void
shot_capture_done (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  OozeShotWindow *self = OOZE_SHOT_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GVariant) reply = NULL;
  const char *path;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                         result, &error);
  if (!reply)
    {
      g_autofree char *message =
        g_strdup_printf ("Capture failed: %s",
                         error ? error->message : "unknown error");
      shot_capture_failed (self, message);
      g_object_unref (self);
      return;
    }

  g_variant_get (reply, "(&s)", &path);
  g_clear_pointer (&self->path, g_free);
  self->path = g_strdup (path);
  g_clear_object (&self->texture);
  file = g_file_new_for_path (path);
  self->texture = gdk_texture_new_from_file (file, &error);
  if (!self->texture)
    {
      g_autofree char *message =
        g_strdup_printf ("Saved, but preview failed: %s",
                         error ? error->message : "unknown error");
      shot_capture_failed (self, message);
      g_object_unref (self);
      return;
    }

  gtk_picture_set_paintable (GTK_PICTURE (self->preview),
                             GDK_PAINTABLE (self->texture));
  gtk_widget_set_visible (self->preview, TRUE);
  gtk_widget_set_sensitive (self->copy, TRUE);
  gtk_widget_set_sensitive (self->reveal, TRUE);
  gtk_widget_set_sensitive (self->eye, TRUE);
  self->capture_in_flight = FALSE;
  gtk_widget_set_sensitive (self->capture, TRUE);
  shot_show_window (self);
  shot_set_status (self, self->path);
  g_object_unref (self);
}

static void
shot_call_capture (OozeShotWindow *self)
{
  g_dbus_connection_call (self->session,
                          SHOT_BUS_NAME,
                          SHOT_OBJECT_PATH,
                          SHOT_INTERFACE,
                          "CaptureDesktop",
                          NULL,
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          shot_capture_done,
                          g_object_ref (self));
}

static void
shot_session_ready (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  OozeShotWindow *self = OOZE_SHOT_WINDOW (user_data);
  g_autoptr (GError) error = NULL;

  self->session = g_bus_get_finish (result, &error);
  if (!self->session)
    {
      g_autofree char *message =
        g_strdup_printf ("Ooze Shot service unavailable: %s",
                         error ? error->message : "unknown error");
      shot_capture_failed (self, message);
    }
  else
    {
      shot_call_capture (self);
    }

  (void) source;
  g_object_unref (self);
}

static void
shot_begin_capture (OozeShotWindow *self)
{
  if (!self->session)
    {
      g_bus_get (G_BUS_TYPE_SESSION, NULL, shot_session_ready,
                 g_object_ref (self));
      return;
    }

  shot_call_capture (self);
}

static gboolean
shot_delay_cb (gpointer user_data)
{
  OozeShotWindow *self = OOZE_SHOT_WINDOW (user_data);

  self->delay_id = 0;
  shot_begin_capture (self);
  return G_SOURCE_REMOVE;
}

static void
shot_capture_clicked (GtkButton     *button G_GNUC_UNUSED,
                      OozeShotWindow *self)
{
  guint selected;
  guint seconds = 0;

  if (self->capture_in_flight)
    return;

  self->capture_in_flight = TRUE;
  gtk_widget_set_sensitive (self->capture, FALSE);
  shot_set_status (self, "Preparing capture…");

  selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->delay));
  if (selected == 1)
    seconds = 3;
  else if (selected == 2)
    seconds = 5;

  if (seconds > 0)
    {
      self->hidden_for_capture = TRUE;
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
      self->delay_id = g_timeout_add_seconds (seconds, shot_delay_cb, self);
    }
  else
    {
      shot_begin_capture (self);
    }
}

static void
shot_copy_clicked (GtkButton     *button G_GNUC_UNUSED,
                   OozeShotWindow *self)
{
  g_autoptr (GdkContentProvider) provider = NULL;

  if (!self->texture)
    return;

  provider = gdk_content_provider_new_typed (GDK_TYPE_TEXTURE,
                                             self->texture);
  gdk_clipboard_set_content (
    gtk_widget_get_clipboard (GTK_WIDGET (self)), provider);
  shot_set_status (self, "Image copied to the clipboard");
}

static void
shot_reveal_clicked (GtkButton     *button G_GNUC_UNUSED,
                     OozeShotWindow *self)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFile) directory = NULL;
  g_autofree char *uri = NULL;
  g_autoptr (GError) error = NULL;

  if (!self->path)
    return;

  file = g_file_new_for_path (self->path);
  directory = g_file_get_parent (file);
  if (!directory)
    return;

  uri = g_file_get_uri (directory);
  if (!g_app_info_launch_default_for_uri (uri, NULL, &error))
    shot_set_status (self, error ? error->message : "Could not open folder");
}

static void
shot_eye_clicked (GtkButton     *button G_GNUC_UNUSED,
                  OozeShotWindow *self)
{
  g_autoptr (GFile) file = NULL;
  g_autofree char *uri = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GAppInfo) app = NULL;
  GList *uris;

  if (!self->path)
    return;

  file = g_file_new_for_path (self->path);
  uri = g_file_get_uri (file);
  app = g_app_info_create_from_commandline (
    "ooze-eye", "Ooze Eye", G_APP_INFO_CREATE_SUPPORTS_URIS, &error);
  if (!app)
    {
      shot_set_status (self, error ? error->message : "Could not start Ooze Eye");
      return;
    }

  uris = g_list_append (NULL, uri);
  if (!g_app_info_launch_uris (app, uris, NULL, &error))
    shot_set_status (self, error ? error->message : "Could not open Ooze Eye");
  g_list_free (uris);
}

static GtkWidget *
shot_new_choice (const char *const *choices)
{
  return gtk_drop_down_new_from_strings (choices);
}

static GtkWidget *
shot_make_labeled_control (const char *label,
                           GtkWidget  *control)
{
  GtkWidget *row;
  GtkWidget *caption;

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  caption = gtk_label_new (label);
  gtk_widget_set_hexpand (caption, TRUE);
  gtk_label_set_xalign (GTK_LABEL (caption), 0.0);
  gtk_box_append (GTK_BOX (row), caption);
  gtk_box_append (GTK_BOX (row), control);
  return row;
}

static void
ooze_shot_window_constructed (GObject *object)
{
  OozeShotWindow *self = OOZE_SHOT_WINDOW (object);
  static const char *const delays[] = { "None", "3 seconds", "5 seconds", NULL };
  GtkWidget *shell;
  GtkWidget *toolbar;
  GtkWidget *group;
  GtkWidget *controls;
  GtkWidget *surface;
  GtkWidget *actions;
  GtkWidget *placeholder;

  G_OBJECT_CLASS (ooze_shot_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 860, 620);
  gtk_window_set_icon_name (GTK_WINDOW (self), "camera-photo");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-shot");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Ooze Shot");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  toolbar = ooze_toolbar_new ();
  group = ooze_toolbar_add_group (toolbar);
  self->capture = ooze_button_new_labeled (
    OOZE_BUTTON_TOOLBAR, capture_icons, 40, "Capture", "Capture the screen");
  g_signal_connect (self->capture, "clicked",
                    G_CALLBACK (shot_capture_clicked), self);
  gtk_box_append (GTK_BOX (group), self->capture);
  gtk_box_append (GTK_BOX (shell), toolbar);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR,
                              GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);

  controls = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (controls, 20);
  gtk_widget_set_margin_end (controls, 20);
  gtk_widget_set_margin_top (controls, 18);
  gtk_widget_set_margin_bottom (controls, 12);

  self->mode = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 14);
  {
    GtkWidget *screen;
    GtkWidget *area;
    GtkWidget *window;

    screen = gtk_check_button_new_with_label ("Screen");
    area = gtk_check_button_new_with_label ("Area");
    window = gtk_check_button_new_with_label ("Window");
    gtk_check_button_set_group (GTK_CHECK_BUTTON (area),
                                GTK_CHECK_BUTTON (screen));
    gtk_check_button_set_group (GTK_CHECK_BUTTON (window),
                                GTK_CHECK_BUTTON (screen));
    gtk_check_button_set_active (GTK_CHECK_BUTTON (screen), TRUE);
    gtk_widget_set_sensitive (area, FALSE);
    gtk_widget_set_sensitive (window, FALSE);
    gtk_widget_set_tooltip_text (
      area, "Area selection needs compositor region-picker support");
    gtk_widget_set_tooltip_text (
      window, "Window selection needs compositor window-picker support");
    gtk_box_append (GTK_BOX (self->mode), screen);
    gtk_box_append (GTK_BOX (self->mode), area);
    gtk_box_append (GTK_BOX (self->mode), window);
  }
  gtk_box_append (GTK_BOX (controls),
                  shot_make_labeled_control ("Capture mode", self->mode));

  self->delay = shot_new_choice (delays);
  gtk_box_append (GTK_BOX (controls),
                  shot_make_labeled_control ("Delay", self->delay));

  placeholder = gtk_label_new (
    "Screen capture is available now. Area and Window selection are coming soon.");
  gtk_label_set_wrap (GTK_LABEL (placeholder), TRUE);
  gtk_label_set_xalign (GTK_LABEL (placeholder), 0.0);
  gtk_widget_add_css_class (placeholder, "dim-label");
  gtk_box_append (GTK_BOX (controls), placeholder);

  self->preview = gtk_picture_new ();
  gtk_picture_set_can_shrink (GTK_PICTURE (self->preview), TRUE);
  gtk_picture_set_content_fit (GTK_PICTURE (self->preview),
                               GTK_CONTENT_FIT_CONTAIN);
  gtk_widget_set_hexpand (self->preview, TRUE);
  gtk_widget_set_vexpand (self->preview, TRUE);
  gtk_widget_set_visible (self->preview, FALSE);
  gtk_box_append (GTK_BOX (controls), self->preview);

  actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  self->copy = ooze_button_new_labeled (
    OOZE_BUTTON_PUSH, copy_icons, 28, "Copy", "Copy image to clipboard");
  self->reveal = ooze_button_new_labeled (
    OOZE_BUTTON_PUSH, reveal_icons, 28, "Reveal", "Open the screenshot folder");
  self->eye = ooze_button_new_labeled (
    OOZE_BUTTON_PUSH, eye_icons, 28, "Open in Eye", "Open screenshot in Ooze Eye");
  g_signal_connect (self->copy, "clicked",
                    G_CALLBACK (shot_copy_clicked), self);
  g_signal_connect (self->reveal, "clicked",
                    G_CALLBACK (shot_reveal_clicked), self);
  g_signal_connect (self->eye, "clicked",
                    G_CALLBACK (shot_eye_clicked), self);
  gtk_widget_set_sensitive (self->copy, FALSE);
  gtk_widget_set_sensitive (self->reveal, FALSE);
  gtk_widget_set_sensitive (self->eye, FALSE);
  gtk_box_append (GTK_BOX (actions), self->copy);
  gtk_box_append (GTK_BOX (actions), self->reveal);
  gtk_box_append (GTK_BOX (actions), self->eye);
  gtk_box_append (GTK_BOX (controls), actions);

  self->status = gtk_label_new ("Ready");
  gtk_label_set_xalign (GTK_LABEL (self->status), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (self->status), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_add_css_class (self->status, "dim-label");
  gtk_box_append (GTK_BOX (controls), self->status);

  gtk_box_append (GTK_BOX (surface), controls);
  gtk_box_append (GTK_BOX (shell), surface);
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);
}

static void
ooze_shot_window_dispose (GObject *object)
{
  OozeShotWindow *self = OOZE_SHOT_WINDOW (object);

  if (self->delay_id)
    {
      g_source_remove (self->delay_id);
      self->delay_id = 0;
    }
  g_clear_object (&self->session);
  g_clear_object (&self->texture);
  g_clear_pointer (&self->path, g_free);

  G_OBJECT_CLASS (ooze_shot_window_parent_class)->dispose (object);
}

static void
ooze_shot_window_class_init (OozeShotWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_shot_window_constructed;
  object_class->dispose = ooze_shot_window_dispose;
}

static void
ooze_shot_window_init (OozeShotWindow *self G_GNUC_UNUSED)
{
}

GtkWidget *
ooze_shot_window_new (GtkApplication *app)
{
  return g_object_new (OOZE_SHOT_TYPE_WINDOW,
                       "application", app,
                       NULL);
}
