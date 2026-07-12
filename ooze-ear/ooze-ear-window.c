#include "ooze-ear-window.h"
#include "ooze-sound-pane.h"

#include "ooze-header-bar.h"

#include <adwaita.h>

struct _OozeEarWindow
{
  GtkApplicationWindow parent_instance;
  GtkWidget *header;
};

G_DEFINE_FINAL_TYPE (OozeEarWindow, ooze_ear_window, GTK_TYPE_APPLICATION_WINDOW)

static void
ooze_ear_window_class_init (OozeEarWindowClass *klass G_GNUC_UNUSED)
{
}

static void
ooze_ear_window_init (OozeEarWindow *self)
{
  GtkWidget *pane;

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 480);
  gtk_window_set_title (GTK_WINDOW (self), "Sound");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-ear");

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "Sound");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  pane = ooze_sound_pane_new ();
  gtk_window_set_child (GTK_WINDOW (self), pane);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
ooze_ear_window_new (GtkApplication *app)
{
  return g_object_new (OOZE_EAR_TYPE_WINDOW, "application", app, NULL);
}
