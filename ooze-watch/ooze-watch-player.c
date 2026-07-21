#include "ooze-watch-player.h"

#include <epoxy/gl.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <epoxy/egl.h>
#include <gdk/wayland/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <epoxy/glx.h>
#include <gdk/x11/gdkx.h>
#endif

#include <mpv/client.h>
#include <mpv/render_gl.h>

struct _OozeWatchPlayer
{
  GtkGLArea parent_instance;

  mpv_handle         *mpv;
  mpv_render_context *render_ctx;

  double    time_pos;
  double    duration;
  double    volume;   /* 0..100 (mpv scale) */
  gboolean  paused;
  gboolean  has_media;
  char     *title;

  guint     event_idle;
  guint     render_idle;

  GFile    *pending; /* opened before the render context existed */
};

G_DEFINE_FINAL_TYPE (OozeWatchPlayer, ooze_watch_player, GTK_TYPE_GL_AREA)

enum
{
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void *
player_get_proc_address (void *ctx G_GNUC_UNUSED, const char *name)
{
  GdkDisplay *display = gdk_display_get_default ();

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (display))
    return (void *) eglGetProcAddress (name);
#endif
#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_DISPLAY (display))
    {
      void *p = (void *) eglGetProcAddress (name);
      if (!p)
        p = (void *) glXGetProcAddressARB ((const GLubyte *) name);
      return p;
    }
#endif
  return NULL;
}

static gboolean
player_drain_events (gpointer user_data)
{
  OozeWatchPlayer *self = user_data;
  gboolean changed = FALSE;

  self->event_idle = 0;

  if (!self->mpv)
    return G_SOURCE_REMOVE;

  for (;;)
    {
      mpv_event *ev = mpv_wait_event (self->mpv, 0);

      if (ev->event_id == MPV_EVENT_NONE)
        break;

      switch (ev->event_id)
        {
        case MPV_EVENT_PROPERTY_CHANGE:
          {
            mpv_event_property *prop = ev->data;

            if (g_strcmp0 (prop->name, "time-pos") == 0 &&
                prop->format == MPV_FORMAT_DOUBLE)
              self->time_pos = *(double *) prop->data;
            else if (g_strcmp0 (prop->name, "duration") == 0 &&
                     prop->format == MPV_FORMAT_DOUBLE)
              self->duration = *(double *) prop->data;
            else if (g_strcmp0 (prop->name, "volume") == 0 &&
                     prop->format == MPV_FORMAT_DOUBLE)
              self->volume = *(double *) prop->data;
            else if (g_strcmp0 (prop->name, "pause") == 0 &&
                     prop->format == MPV_FORMAT_FLAG)
              self->paused = *(int *) prop->data;
            else if (g_strcmp0 (prop->name, "media-title") == 0 &&
                     prop->format == MPV_FORMAT_STRING)
              {
                g_free (self->title);
                self->title = g_strdup (*(char **) prop->data);
              }
            changed = TRUE;
          }
          break;

        case MPV_EVENT_FILE_LOADED:
          self->has_media = TRUE;
          changed = TRUE;
          break;

        case MPV_EVENT_END_FILE:
          changed = TRUE;
          break;

        default:
          break;
        }
    }

  if (changed)
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

  return G_SOURCE_REMOVE;
}

static void
player_wakeup_cb (void *user_data)
{
  OozeWatchPlayer *self = user_data;

  /* Called from an mpv thread — hop to the main loop. */
  if (g_atomic_int_get ((gint *) &self->event_idle) == 0)
    self->event_idle = g_idle_add (player_drain_events, self);
}

static gboolean
player_queue_render (gpointer user_data)
{
  OozeWatchPlayer *self = user_data;

  self->render_idle = 0;
  gtk_gl_area_queue_render (GTK_GL_AREA (self));
  return G_SOURCE_REMOVE;
}

static void
player_render_update_cb (void *user_data)
{
  OozeWatchPlayer *self = user_data;

  if (g_atomic_int_get ((gint *) &self->render_idle) == 0)
    self->render_idle = g_idle_add (player_queue_render, self);
}

static void
player_realize (GtkWidget *widget)
{
  OozeWatchPlayer *self = OOZE_WATCH_PLAYER (widget);
  mpv_opengl_init_params gl_params = {
    .get_proc_address = player_get_proc_address,
  };
  mpv_render_param params[] = {
    { MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL },
    { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_params },
    { 0, NULL },
  };

  GTK_WIDGET_CLASS (ooze_watch_player_parent_class)->realize (widget);

  if (!self->mpv || gtk_gl_area_get_error (GTK_GL_AREA (self)))
    return;

  gtk_gl_area_make_current (GTK_GL_AREA (self));

  if (mpv_render_context_create (&self->render_ctx, self->mpv, params) < 0)
    {
      g_warning ("ooze-watch: could not create mpv render context");
      self->render_ctx = NULL;
      return;
    }

  mpv_render_context_set_update_callback (self->render_ctx,
                                          player_render_update_cb, self);

  if (self->pending)
    {
      g_autoptr (GFile) file = g_steal_pointer (&self->pending);

      ooze_watch_player_open (self, file);
    }
}

static void
player_unrealize (GtkWidget *widget)
{
  OozeWatchPlayer *self = OOZE_WATCH_PLAYER (widget);

  if (self->render_ctx)
    {
      gtk_gl_area_make_current (GTK_GL_AREA (self));
      mpv_render_context_free (self->render_ctx);
      self->render_ctx = NULL;
    }

  GTK_WIDGET_CLASS (ooze_watch_player_parent_class)->unrealize (widget);
}

static gboolean
player_render (GtkGLArea *area, GdkGLContext *context G_GNUC_UNUSED)
{
  OozeWatchPlayer *self = OOZE_WATCH_PLAYER (area);
  int fbo = -1;
  int flip_y = 1;
  int scale = gtk_widget_get_scale_factor (GTK_WIDGET (area));
  mpv_opengl_fbo mpv_fbo;
  mpv_render_param params[] = {
    { MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo },
    { MPV_RENDER_PARAM_FLIP_Y, &flip_y },
    { 0, NULL },
  };

  glClearColor (0.f, 0.f, 0.f, 1.f);
  glClear (GL_COLOR_BUFFER_BIT);

  if (!self->render_ctx)
    return TRUE;

  glGetIntegerv (GL_FRAMEBUFFER_BINDING, &fbo);
  mpv_fbo.fbo = fbo;
  mpv_fbo.w = gtk_widget_get_width (GTK_WIDGET (area)) * scale;
  mpv_fbo.h = gtk_widget_get_height (GTK_WIDGET (area)) * scale;
  mpv_fbo.internal_format = 0;

  mpv_render_context_render (self->render_ctx, params);
  return TRUE;
}

static void
ooze_watch_player_dispose (GObject *object)
{
  OozeWatchPlayer *self = OOZE_WATCH_PLAYER (object);

  g_clear_handle_id (&self->event_idle, g_source_remove);
  g_clear_handle_id (&self->render_idle, g_source_remove);

  if (self->mpv)
    {
      mpv_set_wakeup_callback (self->mpv, NULL, NULL);
      mpv_terminate_destroy (self->mpv);
      self->mpv = NULL;
    }
  g_clear_pointer (&self->title, g_free);
  g_clear_object (&self->pending);

  G_OBJECT_CLASS (ooze_watch_player_parent_class)->dispose (object);
}

static void
ooze_watch_player_class_init (OozeWatchPlayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkGLAreaClass *area_class = GTK_GL_AREA_CLASS (klass);

  object_class->dispose = ooze_watch_player_dispose;
  widget_class->realize = player_realize;
  widget_class->unrealize = player_unrealize;
  area_class->render = player_render;

  signals[SIGNAL_CHANGED] =
    g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
ooze_watch_player_init (OozeWatchPlayer *self)
{
  self->volume = 100.0;
  self->paused = TRUE;

  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self), TRUE);

  self->mpv = mpv_create ();
  if (!self->mpv)
    {
      g_warning ("ooze-watch: mpv_create failed");
      return;
    }

  mpv_set_option_string (self->mpv, "vo", "libmpv");
  mpv_set_option_string (self->mpv, "keep-open", "yes");
  mpv_set_option_string (self->mpv, "audio-client-name", "Ooze Watch");
  mpv_set_option_string (self->mpv, "osc", "no");
  mpv_set_option_string (self->mpv, "input-default-bindings", "no");

  if (g_getenv ("OOZE_WATCH_DEBUG"))
    {
      mpv_set_option_string (self->mpv, "terminal", "yes");
      mpv_set_option_string (self->mpv, "msg-level", "all=v");
    }

  if (mpv_initialize (self->mpv) < 0)
    {
      g_warning ("ooze-watch: mpv_initialize failed");
      mpv_terminate_destroy (self->mpv);
      self->mpv = NULL;
      return;
    }

  mpv_observe_property (self->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
  mpv_observe_property (self->mpv, 0, "duration", MPV_FORMAT_DOUBLE);
  mpv_observe_property (self->mpv, 0, "volume", MPV_FORMAT_DOUBLE);
  mpv_observe_property (self->mpv, 0, "pause", MPV_FORMAT_FLAG);
  mpv_observe_property (self->mpv, 0, "media-title", MPV_FORMAT_STRING);
  mpv_set_wakeup_callback (self->mpv, player_wakeup_cb, self);
}

GtkWidget *
ooze_watch_player_new (void)
{
  return g_object_new (OOZE_WATCH_TYPE_PLAYER, NULL);
}

void
ooze_watch_player_open (OozeWatchPlayer *self, GFile *file)
{
  g_autofree char *path = NULL;

  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));
  g_return_if_fail (G_IS_FILE (file));

  if (!self->mpv)
    return;

  if (!self->render_ctx)
    {
      /* mpv's libmpv VO refuses files until a render context exists;
       * defer until realize. */
      g_set_object (&self->pending, file);
      return;
    }

  path = g_file_get_path (file);
  if (!path)
    path = g_file_get_uri (file);
  {
    const char *cmd[] = { "loadfile", path, NULL };
    mpv_command_async (self->mpv, 0, cmd);
  }
  {
    int no = 0;
    mpv_set_property_async (self->mpv, 0, "pause", MPV_FORMAT_FLAG, &no);
  }
}

void
ooze_watch_player_toggle_pause (OozeWatchPlayer *self)
{
  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));

  if (!self->mpv || !self->has_media)
    return;
  {
    const char *cmd[] = { "cycle", "pause", NULL };
    mpv_command_async (self->mpv, 0, cmd);
  }
}

void
ooze_watch_player_seek_frac (OozeWatchPlayer *self, double frac)
{
  g_autofree char *pos = NULL;

  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));

  if (!self->mpv || !self->has_media || self->duration <= 0)
    return;

  pos = g_strdup_printf ("%.3f", CLAMP (frac, 0.0, 1.0) * self->duration);
  {
    const char *cmd[] = { "seek", pos, "absolute", NULL };
    mpv_command_async (self->mpv, 0, cmd);
  }
}

void
ooze_watch_player_seek_rel (OozeWatchPlayer *self, double secs)
{
  g_autofree char *amount = NULL;

  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));

  if (!self->mpv || !self->has_media)
    return;

  amount = g_strdup_printf ("%.3f", secs);
  {
    const char *cmd[] = { "seek", amount, "relative", NULL };
    mpv_command_async (self->mpv, 0, cmd);
  }
}

void
ooze_watch_player_frame_step (OozeWatchPlayer *self, gboolean back)
{
  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));

  if (!self->mpv || !self->has_media)
    return;
  {
    const char *cmd[] = { back ? "frame-back-step" : "frame-step", NULL };
    mpv_command_async (self->mpv, 0, cmd);
  }
}

void
ooze_watch_player_set_volume (OozeWatchPlayer *self, double vol01)
{
  double vol;

  g_return_if_fail (OOZE_WATCH_IS_PLAYER (self));

  if (!self->mpv)
    return;

  vol = CLAMP (vol01, 0.0, 1.0) * 100.0;
  self->volume = vol;
  mpv_set_property_async (self->mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol);
}

double
ooze_watch_player_get_volume (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), 1.0);
  return CLAMP (self->volume / 100.0, 0.0, 1.0);
}

gboolean
ooze_watch_player_get_paused (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), TRUE);
  return self->paused;
}

double
ooze_watch_player_get_time (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), 0.0);
  return MAX (self->time_pos, 0.0);
}

double
ooze_watch_player_get_duration (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), 0.0);
  return MAX (self->duration, 0.0);
}

const char *
ooze_watch_player_get_title (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), NULL);
  return self->title;
}

gboolean
ooze_watch_player_has_media (OozeWatchPlayer *self)
{
  g_return_val_if_fail (OOZE_WATCH_IS_PLAYER (self), FALSE);
  return self->has_media;
}
