#include "ooze-watch-window.h"
#include "ooze-watch-player.h"
#include "ooze-watch-transport.h"

#include "ooze-about.h"

struct _OozeWatchWindow
{
  OozeApplicationWindow parent_instance;

  GtkWidget *player;
  GtkWidget *transport;
};

G_DEFINE_FINAL_TYPE (OozeWatchWindow, ooze_watch_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void ooze_watch_window_constructed (GObject *object);

static void
watch_sync_transport (OozeWatchWindow *self)
{
  OozeWatchPlayer *player = OOZE_WATCH_PLAYER (self->player);
  const char *title = ooze_watch_player_get_title (player);

  ooze_watch_transport_set_state (
    OOZE_WATCH_TRANSPORT (self->transport),
    ooze_watch_player_get_time (player),
    ooze_watch_player_get_duration (player),
    ooze_watch_player_get_paused (player),
    ooze_watch_player_get_volume (player),
    ooze_watch_player_has_media (player));

  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self),
    (title && *title) ? title : "Ooze Watch");
}

static void
on_player_changed (OozeWatchPlayer *player G_GNUC_UNUSED,
                   gpointer         user_data)
{
  watch_sync_transport (OOZE_WATCH_WINDOW (user_data));
}

static void
on_play_toggled (OozeWatchTransport *transport G_GNUC_UNUSED,
                 gpointer            user_data)
{
  OozeWatchWindow *self = user_data;

  if (ooze_watch_player_has_media (OOZE_WATCH_PLAYER (self->player)))
    ooze_watch_player_toggle_pause (OOZE_WATCH_PLAYER (self->player));
  else
    gtk_widget_activate_action (GTK_WIDGET (self), "win.open", NULL);
}

static void
on_seek_frac (OozeWatchTransport *transport G_GNUC_UNUSED,
              double              frac,
              gpointer            user_data)
{
  OozeWatchWindow *self = user_data;

  ooze_watch_player_seek_frac (OOZE_WATCH_PLAYER (self->player), frac);
}

static void
on_volume (OozeWatchTransport *transport G_GNUC_UNUSED,
           double              vol01,
           gpointer            user_data)
{
  OozeWatchWindow *self = user_data;

  ooze_watch_player_set_volume (OOZE_WATCH_PLAYER (self->player), vol01);
}

static void
on_step (OozeWatchTransport *transport G_GNUC_UNUSED,
         int                 direction,
         gpointer            user_data)
{
  OozeWatchWindow *self = user_data;

  ooze_watch_player_frame_step (OOZE_WATCH_PLAYER (self->player),
                                direction < 0);
}

static void
on_jump (OozeWatchTransport *transport G_GNUC_UNUSED,
         int                 direction,
         gpointer            user_data)
{
  OozeWatchWindow *self = user_data;

  ooze_watch_player_seek_rel (OOZE_WATCH_PLAYER (self->player),
                              direction * 10.0);
}

static void
on_open_response (GObject *source, GAsyncResult *result, gpointer user_data)
{
  OozeWatchWindow *self = user_data;
  g_autoptr (GFile) file = NULL;

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, NULL);
  if (file)
    ooze_watch_window_open_file (self, file);
  g_object_unref (self);
}

static void
watch_action_open (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  OozeWatchWindow *self = user_data;
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  g_autoptr (GtkFileFilter) filter = gtk_file_filter_new ();
  g_autoptr (GListStore) filters =
    g_list_store_new (GTK_TYPE_FILE_FILTER);

  gtk_file_filter_set_name (filter, "Video and audio");
  gtk_file_filter_add_mime_type (filter, "video/*");
  gtk_file_filter_add_mime_type (filter, "audio/*");
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_set_title (dialog, "Open Media");

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL,
                        on_open_response, g_object_ref (self));
}

static void
watch_action_about (GSimpleAction *action G_GNUC_UNUSED,
                    GVariant      *param G_GNUC_UNUSED,
                    gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data), "Ooze Watch",
                      "multimedia-video-player",
                      "Retro movie player powered by mpv.",
                      OOZE_VERSION);
}

static gboolean
on_key_pressed (GtkEventControllerKey *ctl G_GNUC_UNUSED,
                guint keyval, guint keycode G_GNUC_UNUSED,
                GdkModifierType state, gpointer user_data)
{
  OozeWatchWindow *self = user_data;
  OozeWatchPlayer *player = OOZE_WATCH_PLAYER (self->player);

  if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK))
    return FALSE;

  switch (keyval)
    {
    case GDK_KEY_space:
      ooze_watch_player_toggle_pause (player);
      return TRUE;
    case GDK_KEY_Left:
      ooze_watch_player_seek_rel (player, -10.0);
      return TRUE;
    case GDK_KEY_Right:
      ooze_watch_player_seek_rel (player, 10.0);
      return TRUE;
    default:
      return FALSE;
    }
}

static gboolean
on_file_dropped (GtkDropTarget *target G_GNUC_UNUSED,
                 const GValue *value,
                 double x G_GNUC_UNUSED, double y G_GNUC_UNUSED,
                 gpointer user_data)
{
  OozeWatchWindow *self = user_data;

  if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    {
      GSList *files = g_value_get_boxed (value);

      if (files && files->data)
        {
          ooze_watch_window_open_file (self, G_FILE (files->data));
          return TRUE;
        }
    }
  return FALSE;
}

static GMenuModel *
watch_build_file_menu (void)
{
  GMenu *file = g_menu_new ();

  g_menu_append (file, "Open…", "win.open");
  return G_MENU_MODEL (file);
}

static GMenuModel *
watch_build_help_menu (void)
{
  GMenu *help = g_menu_new ();

  g_menu_append (help, "About Ooze Watch", "win.about");
  return G_MENU_MODEL (help);
}

static void
ooze_watch_window_class_init (OozeWatchWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_watch_window_constructed;
}

static void
ooze_watch_window_init (OozeWatchWindow *self)
{
  static const GActionEntry entries[] = {
    { "open", watch_action_open, NULL, NULL, NULL },
    { "about", watch_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self), entries,
                                   G_N_ELEMENTS (entries), self);
}

static void
ooze_watch_window_constructed (GObject *object)
{
  OozeWatchWindow *self = OOZE_WATCH_WINDOW (object);
  GtkWidget *box;
  GMenuModel *menu;
  GtkEventController *keys;
  GtkDropTarget *drop;

  G_OBJECT_CLASS (ooze_watch_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 720, 520);
  gtk_window_set_icon_name (GTK_WINDOW (self), "multimedia-video-player");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-watch");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Ooze Watch");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->player = ooze_watch_player_new ();
  gtk_box_append (GTK_BOX (box), self->player);

  self->transport = ooze_watch_transport_new ();
  gtk_box_append (GTK_BOX (box), self->transport);

  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), box);

  g_signal_connect (self->player, "changed",
                    G_CALLBACK (on_player_changed), self);
  g_signal_connect (self->transport, "play-toggled",
                    G_CALLBACK (on_play_toggled), self);
  g_signal_connect (self->transport, "seek-frac",
                    G_CALLBACK (on_seek_frac), self);
  g_signal_connect (self->transport, "volume",
                    G_CALLBACK (on_volume), self);
  g_signal_connect (self->transport, "step",
                    G_CALLBACK (on_step), self);
  g_signal_connect (self->transport, "jump",
                    G_CALLBACK (on_jump), self);

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed",
                    G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect (drop, "drop", G_CALLBACK (on_file_dropped), self);
  gtk_widget_add_controller (GTK_WIDGET (self),
                             GTK_EVENT_CONTROLLER (drop));

  menu = watch_build_file_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "File", menu);
  g_object_unref (menu);

  menu = watch_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", menu);
  g_object_unref (menu);

  watch_sync_transport (self);
}

GtkWidget *
ooze_watch_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_WATCH_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}

void
ooze_watch_window_open_file (OozeWatchWindow *self, GFile *file)
{
  g_return_if_fail (OOZE_WATCH_IS_WINDOW (self));

  ooze_watch_player_open (OOZE_WATCH_PLAYER (self->player), file);
}
