#include "ooze-pinline.h"

#include "ooze-palette.h"

#include <adwaita.h>

static void
ooze_pinline_draw (GtkDrawingArea *area G_GNUC_UNUSED,
                   cairo_t        *cr,
                   int             width,
                   int             height,
                   gpointer        user_data)
{
  OozeSide side = GPOINTER_TO_INT (user_data);
  const OozePalette *pal = ooze_palette_current ();

  if (side == OOZE_SIDE_LEFT || side == OOZE_SIDE_RIGHT)
    ooze_draw_separator (cr, width, height, OOZE_SIDE_RIGHT, pal);
  else
    ooze_draw_separator (cr, width, height, OOZE_SIDE_BOTTOM, pal);
}

static void
on_dark_changed (GObject    *obj G_GNUC_UNUSED,
                 GParamSpec *pspec G_GNUC_UNUSED,
                 GtkWidget  *widget)
{
  gtk_widget_queue_draw (widget);
}

GtkWidget *
ooze_pinline_new (OozeSide side)
{
  GtkWidget *widget;
  gboolean vertical = (side == OOZE_SIDE_LEFT || side == OOZE_SIDE_RIGHT);

  widget = gtk_drawing_area_new ();
  gtk_widget_set_can_target (widget, FALSE);
  gtk_widget_set_can_focus (widget, FALSE);
  gtk_widget_add_css_class (widget, "ooze-pinline");

  if (vertical)
    {
      gtk_widget_set_size_request (widget, 2, -1);
      gtk_widget_set_hexpand (widget, FALSE);
      gtk_widget_set_vexpand (widget, TRUE);
      gtk_widget_set_halign (widget, GTK_ALIGN_FILL);
      gtk_widget_set_valign (widget, GTK_ALIGN_FILL);
    }
  else
    {
      gtk_widget_set_size_request (widget, -1, 2);
      gtk_widget_set_hexpand (widget, TRUE);
      gtk_widget_set_vexpand (widget, FALSE);
      gtk_widget_set_halign (widget, GTK_ALIGN_FILL);
      gtk_widget_set_valign (widget, GTK_ALIGN_FILL);
    }

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (widget),
                                  ooze_pinline_draw,
                                  GINT_TO_POINTER (side),
                                  NULL);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (on_dark_changed), widget, 0);

  return widget;
}
