#include "ooze-popover.h"

static int ooze_popover_monitor_max_height (GtkPopover *popover);

static void
ooze_popover_fix_scrolled (GtkWidget *widget,
                            int        max_height)
{
  for (GtkWidget *c = gtk_widget_get_first_child (widget);
       c != NULL;
       c = gtk_widget_get_next_sibling (c))
    {
      if (GTK_IS_SCROLLED_WINDOW (c))
        {
          GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (c);

          gtk_scrolled_window_set_propagate_natural_height (sw, TRUE);
          gtk_scrolled_window_set_propagate_natural_width (sw, TRUE);
          gtk_scrolled_window_set_min_content_height (sw, 0);
          gtk_scrolled_window_set_min_content_width (sw, 0);
          gtk_scrolled_window_set_max_content_height (sw, max_height);
          gtk_scrolled_window_set_max_content_width (sw, G_MAXINT);
          /* Scroll only when content exceeds max_height. */
          gtk_scrolled_window_set_policy (sw,
                                          GTK_POLICY_NEVER,
                                          GTK_POLICY_AUTOMATIC);
        }

      ooze_popover_fix_scrolled (c, max_height);
    }
}

static gboolean
ooze_popover_fix_scrolled_idle (gpointer user_data)
{
  GtkPopover *popover = user_data;

  ooze_popover_fix_scrolled (
    GTK_WIDGET (popover), ooze_popover_monitor_max_height (popover));
  g_object_unref (popover);
  return G_SOURCE_REMOVE;
}

static int
ooze_popover_monitor_max_height (GtkPopover *popover)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  GdkDisplay *display;
  GdkSurface *surface = NULL;
  GdkMonitor *monitor = NULL;
  gboolean owned = FALSE;
  GdkRectangle geom;
  const int margin = 24;
  int max_h = 2000;

  display = gtk_widget_get_display (widget);
  if (!display)
    return max_h;

  if (gtk_widget_get_mapped (widget))
    {
      GtkNative *native = gtk_widget_get_native (widget);
      if (native)
        surface = gtk_native_get_surface (native);
    }

  if (surface)
    monitor = gdk_display_get_monitor_at_surface (display, surface);

  if (!monitor)
    {
      GListModel *monitors = gdk_display_get_monitors (display);
      if (monitors && g_list_model_get_n_items (monitors) > 0)
        {
          monitor = g_list_model_get_item (monitors, 0); /* transfer full */
          owned = TRUE;
        }
    }

  if (monitor && GDK_IS_MONITOR (monitor))
    {
      gdk_monitor_get_geometry (monitor, &geom);
      max_h = geom.height - margin * 2;
      if (max_h < 120)
        max_h = 120;
    }

  if (owned)
    g_object_unref (monitor);

  return max_h;
}

static void
on_popover_map (GtkWidget *widget,
                gpointer   user_data G_GNUC_UNUSED)
{
  int max_h;

  if (!GTK_IS_POPOVER (widget))
    return;

  max_h = ooze_popover_monitor_max_height (GTK_POPOVER (widget));
  ooze_popover_fix_scrolled (widget, max_h);
  g_idle_add (ooze_popover_fix_scrolled_idle, g_object_ref (widget));
}

void
ooze_popover_fit_screen (GtkPopover *popover)
{
  int max_h;

  g_return_if_fail (GTK_IS_POPOVER (popover));

  max_h = ooze_popover_monitor_max_height (popover);
  ooze_popover_fix_scrolled (GTK_WIDGET (popover), max_h);
  g_idle_add (ooze_popover_fix_scrolled_idle, g_object_ref (popover));

  if (!g_object_get_data (G_OBJECT (popover), "ooze-popover-fit-connected"))
    {
      g_signal_connect (popover, "map", G_CALLBACK (on_popover_map), NULL);
      g_object_set_data (G_OBJECT (popover), "ooze-popover-fit-connected",
                         GINT_TO_POINTER (1));
    }
}
