#include "ooze-window-actions.h"

static void
action_close (GSimpleAction *action G_GNUC_UNUSED,
              GVariant      *param G_GNUC_UNUSED,
              gpointer       user_data)
{
  gtk_window_close (GTK_WINDOW (user_data));
}

static void
action_minimize (GSimpleAction *action G_GNUC_UNUSED,
                 GVariant      *param G_GNUC_UNUSED,
                 gpointer       user_data)
{
  gtk_window_minimize (GTK_WINDOW (user_data));
}

static void
action_maximize (GSimpleAction *action G_GNUC_UNUSED,
                 GVariant      *param G_GNUC_UNUSED,
                 gpointer       user_data)
{
  GtkWindow *win = GTK_WINDOW (user_data);

  if (gtk_window_is_maximized (win))
    gtk_window_unmaximize (win);
  else
    gtk_window_maximize (win);
}

static void
activate_on_focus (GtkWindow *win, const char *detailed_action)
{
  GtkWidget *focus = gtk_window_get_focus (win);

  if (focus)
    gtk_widget_activate_action (focus, detailed_action, NULL);
}

static void
action_cut (GSimpleAction *action G_GNUC_UNUSED,
            GVariant      *param G_GNUC_UNUSED,
            gpointer       user_data)
{
  activate_on_focus (GTK_WINDOW (user_data), "clipboard.cut");
}

static void
action_copy (GSimpleAction *action G_GNUC_UNUSED,
             GVariant      *param G_GNUC_UNUSED,
             gpointer       user_data)
{
  activate_on_focus (GTK_WINDOW (user_data), "clipboard.copy");
}

static void
action_paste (GSimpleAction *action G_GNUC_UNUSED,
              GVariant      *param G_GNUC_UNUSED,
              gpointer       user_data)
{
  activate_on_focus (GTK_WINDOW (user_data), "clipboard.paste");
}

static void
action_select_all (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  activate_on_focus (GTK_WINDOW (user_data), "selection.select-all");
}

void
ooze_window_actions_add_chrome (GtkApplicationWindow *window)
{
  static const GActionEntry entries[] = {
    { .name = "close",    .activate = action_close },
    { .name = "minimize", .activate = action_minimize },
    { .name = "maximize", .activate = action_maximize },
  };

  g_return_if_fail (GTK_IS_APPLICATION_WINDOW (window));
  g_action_map_add_action_entries (G_ACTION_MAP (window),
                                   entries, G_N_ELEMENTS (entries),
                                   window);
}

void
ooze_window_actions_add_edit (GtkApplicationWindow *window)
{
  static const GActionEntry entries[] = {
    { .name = "cut",        .activate = action_cut },
    { .name = "copy",       .activate = action_copy },
    { .name = "paste",      .activate = action_paste },
    { .name = "select-all", .activate = action_select_all },
  };

  g_return_if_fail (GTK_IS_APPLICATION_WINDOW (window));
  g_action_map_add_action_entries (G_ACTION_MAP (window),
                                   entries, G_N_ELEMENTS (entries),
                                   window);
}

void
ooze_menubar_append_edit (GMenu *bar)
{
  GMenu *edit;
  GMenuItem *item;

  g_return_if_fail (G_IS_MENU (bar));

  edit = g_menu_new ();
  g_menu_append (edit, "Cut", "win.cut");
  g_menu_append (edit, "Copy", "win.copy");
  g_menu_append (edit, "Paste", "win.paste");
  g_menu_append (edit, "Select All", "win.select-all");
  item = g_menu_item_new_submenu ("Edit", G_MENU_MODEL (edit));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (edit);
}

void
ooze_menubar_append_window (GMenu *bar)
{
  GMenu *window;
  GMenuItem *item;

  g_return_if_fail (G_IS_MENU (bar));

  window = g_menu_new ();
  g_menu_append (window, "Minimize", "win.minimize");
  g_menu_append (window, "Maximize", "win.maximize");
  g_menu_append (window, "Close", "win.close");
  item = g_menu_item_new_submenu ("Window", G_MENU_MODEL (window));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (window);
}
