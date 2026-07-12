#include "ooze-about.h"

#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-icons.h"
#include "ooze-surface.h"

#include <adwaita.h>

#define OOZE_ABOUT_APP_KEY "ooze-about-app-window"

static void
on_about_destroyed (GtkWidget *win G_GNUC_UNUSED,
                    GtkWindow *parent)
{
  if (GTK_IS_WINDOW (parent))
    g_object_set_data (G_OBJECT (parent), OOZE_ABOUT_APP_KEY, NULL);
}

static void
on_about_ok (GtkButton *btn G_GNUC_UNUSED,
             GtkWindow *win)
{
  gtk_window_destroy (win);
}

void
ooze_about_present (GtkWindow  *parent,
                    const char *brand_name,
                    const char *icon_name,
                    const char *comments,
                    const char *version)
{
  GtkWidget *existing;
  GtkWidget *win;
  GtkWidget *header;
  GtkWidget *surface;
  GtkWidget *box;
  GtkWidget *icon;
  GtkWidget *name_label;
  GtkWidget *ok;
  g_autofree char *title = NULL;
  g_autofree char *version_text = NULL;
  const char *icon_names[3];
  guint n_icons = 0;

  g_return_if_fail (GTK_IS_WINDOW (parent));
  g_return_if_fail (brand_name != NULL && brand_name[0] != '\0');

  existing = g_object_get_data (G_OBJECT (parent), OOZE_ABOUT_APP_KEY);
  if (GTK_IS_WINDOW (existing))
    {
      gtk_window_present (GTK_WINDOW (existing));
      return;
    }

  title = g_strdup_printf ("About %s", brand_name);

  win = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (win), title);
  gtk_window_set_transient_for (GTK_WINDOW (win), parent);
  gtk_window_set_modal (GTK_WINDOW (win), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (win), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (win), 360, 320);
  gtk_widget_add_css_class (win, "ooze-about-app");
  if (icon_name && icon_name[0] != '\0')
    gtk_window_set_icon_name (GTK_WINDOW (win), icon_name);

  g_object_set_data (G_OBJECT (parent), OOZE_ABOUT_APP_KEY, win);
  g_signal_connect (win, "destroy", G_CALLBACK (on_about_destroyed), parent);

  header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (header), GTK_WINDOW (win));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (header), title);
  gtk_window_set_titlebar (GTK_WINDOW (win), header);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_window_set_child (GTK_WINDOW (win), surface);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_widget_set_vexpand (box, TRUE);
  gtk_widget_set_margin_start (box, 28);
  gtk_widget_set_margin_end (box, 28);
  gtk_widget_set_margin_top (box, 20);
  gtk_widget_set_margin_bottom (box, 24);
  gtk_box_append (GTK_BOX (surface), box);

  if (icon_name && icon_name[0] != '\0')
    icon_names[n_icons++] = icon_name;
  icon_names[n_icons++] = "application-x-executable";
  icon_names[n_icons] = NULL;

  icon = ooze_icon_image_new (icon_names, 64);
  gtk_widget_set_halign (icon, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), icon);

  name_label = gtk_label_new (brand_name);
  gtk_label_set_wrap (GTK_LABEL (name_label), TRUE);
  gtk_label_set_justify (GTK_LABEL (name_label), GTK_JUSTIFY_CENTER);
  gtk_widget_add_css_class (name_label, "title-1");
  gtk_widget_add_css_class (name_label, "ooze-emphasis");
  gtk_widget_set_halign (name_label, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), name_label);

  if (version && version[0] != '\0')
    {
      GtkWidget *ver;

      version_text = g_strdup_printf ("Version %s", version);
      ver = gtk_label_new (version_text);
      gtk_widget_set_halign (ver, GTK_ALIGN_CENTER);
      gtk_widget_set_opacity (ver, 0.85);
      gtk_box_append (GTK_BOX (box), ver);
    }

  if (comments && comments[0] != '\0')
    {
      GtkWidget *notes = gtk_label_new (comments);
      gtk_label_set_wrap (GTK_LABEL (notes), TRUE);
      gtk_label_set_justify (GTK_LABEL (notes), GTK_JUSTIFY_CENTER);
      gtk_label_set_max_width_chars (GTK_LABEL (notes), 36);
      gtk_widget_set_halign (notes, GTK_ALIGN_CENTER);
      gtk_widget_set_margin_top (notes, 4);
      gtk_box_append (GTK_BOX (box), notes);
    }

  {
    GtkWidget *dev = gtk_label_new ("Ooze");
    gtk_widget_set_halign (dev, GTK_ALIGN_CENTER);
    gtk_widget_set_opacity (dev, 0.7);
    gtk_widget_set_margin_top (dev, 8);
    gtk_box_append (GTK_BOX (box), dev);
  }

  ok = ooze_button_new (OOZE_BUTTON_PUSH);
  gtk_button_set_label (GTK_BUTTON (ok), "OK");
  gtk_widget_set_halign (ok, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (ok, 12);
  gtk_widget_set_size_request (ok, 88, -1);
  g_signal_connect (ok, "clicked", G_CALLBACK (on_about_ok), win);
  gtk_box_append (GTK_BOX (box), ok);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), win,
                           G_CONNECT_SWAPPED);

  gtk_window_present (GTK_WINDOW (win));
}
