#include "ooze-king-window.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-icons.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"
#include "ooze-window-actions.h"

#include <adwaita.h>
#include <gio/gio.h>

struct _OozeKingWindow
{
  GtkApplicationWindow parent_instance;
  GtkWidget *header;
  GtkWidget *grid;
};

G_DEFINE_FINAL_TYPE (OozeKingWindow, ooze_king_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct {
  const char * const *icons;
  const char         *label;
  const char         *command;
} KingAppEntry;

static const char * const king_icon_spot[] = {
  "org.ooze.Spot", "system-file-manager", NULL
};
static const char * const king_icon_command[] = {
  "org.ooze.Command", "utilities-terminal", NULL
};
static const char * const king_icon_ear[] = {
  "org.ooze.Ear", "audio-headphones", "multimedia-volume-control", NULL
};
static const char * const king_icon_monitor[] = {
  "org.ooze.Monitor", "video-display", "preferences-desktop-display", NULL
};
static const char * const king_icon_about[] = {
  "org.ooze.About", "help-about", "dialog-information", NULL
};
static const char * const king_icon_pak[] = {
  "org.ooze.Pak", "system-software-install", "package-x-generic", NULL
};

static const KingAppEntry king_apps[] = {
  { king_icon_about,   "About This Computer", "ooze-about" },
  { king_icon_monitor, "Displays",            "ooze-monitor" },
  { king_icon_spot,    "File Manager",        "spot" },
  { king_icon_command, "Terminal",       "ooze-command" },
  { king_icon_ear,     "Sound Settings", "ooze-ear" },
  { king_icon_pak,     "Software",       "ooze-pak" },
};

static void
king_action_about (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *param G_GNUC_UNUSED,
                   gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze King",
                      "preferences-system",
                      "System Settings for Ooze Desktop.",
                      OOZE_VERSION);
}

static GMenuModel *
king_build_menubar (void)
{
  GMenu *bar, *help;
  GMenuItem *item;

  bar = g_menu_new ();
  ooze_menubar_append_edit (bar);
  ooze_menubar_append_window (bar);
  help = g_menu_new ();
  g_menu_append (help, "About Ooze King", "win.about");
  item = g_menu_item_new_submenu ("Help", G_MENU_MODEL (help));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (help);
  return G_MENU_MODEL (bar);
}

static void
king_launch_command (const char *command)
{
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *exe = NULL;

  if (!command || !*command)
    return;

  exe = g_find_program_in_path (command);
  if (!exe)
    {
      g_warning ("Ooze King: %s not found in PATH", command);
      return;
    }

  info = g_app_info_create_from_commandline (exe, NULL,
                                             G_APP_INFO_CREATE_NONE,
                                             &error);
  if (!info)
    {
      g_warning ("Ooze King: failed to create launcher for %s: %s",
                 command, error ? error->message : "unknown");
      return;
    }

  if (!g_app_info_launch (info, NULL, NULL, &error))
    g_warning ("Ooze King: failed to launch %s: %s",
               command, error ? error->message : "unknown");
}

static void
on_app_tile_clicked (GtkButton *btn, gpointer user_data G_GNUC_UNUSED)
{
  const char *command = g_object_get_data (G_OBJECT (btn), "command");
  king_launch_command (command);
}

static GtkWidget *
make_launcher_tile (const KingAppEntry *entry)
{
  GtkWidget *btn;

  btn = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR,
                                 entry->icons,
                                 OOZE_ICON_SIZE_GRID,
                                 entry->label,
                                 entry->label);
  gtk_widget_add_css_class (btn, "ooze-launcher-tile");
  g_object_set_data (G_OBJECT (btn), "command", (gpointer) entry->command);
  g_signal_connect (btn, "clicked", G_CALLBACK (on_app_tile_clicked), NULL);
  return btn;
}

static void
ooze_king_window_class_init (OozeKingWindowClass *klass G_GNUC_UNUSED)
{
}

static void
ooze_king_window_init (OozeKingWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", king_action_about, NULL, NULL, NULL },
  };
  GtkWidget *shell;
  GtkWidget *surface;
  gsize i;

  ooze_toolbar_ensure_css ();

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 420);
  gtk_window_set_title (GTK_WINDOW (self), "System Settings");
  gtk_window_set_icon_name (GTK_WINDOW (self), "preferences-system");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-king");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
  ooze_window_actions_add_chrome (GTK_APPLICATION_WINDOW (self));
  ooze_window_actions_add_edit (GTK_APPLICATION_WINDOW (self));

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "System Settings");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_box_append (GTK_BOX (shell), surface);

  self->grid = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->grid), 4);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->grid), 2);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid), TRUE);
  gtk_widget_add_css_class (self->grid, "ooze-launcher-grid");
  gtk_widget_set_halign (self->grid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->grid, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (self->grid, TRUE);
  gtk_widget_set_vexpand (self->grid, TRUE);
  gtk_box_append (GTK_BOX (surface), self->grid);

  for (i = 0; i < G_N_ELEMENTS (king_apps); i++)
    gtk_flow_box_append (GTK_FLOW_BOX (self->grid),
                         make_launcher_tile (&king_apps[i]));

  gtk_window_set_child (GTK_WINDOW (self), shell);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
ooze_king_window_new (GtkApplication *app)
{
  OozeKingWindow *win;
  GMenuModel *menubar;

  win = g_object_new (OOZE_KING_TYPE_WINDOW, "application", app, NULL);

  menubar = king_build_menubar ();
  gtk_application_set_menubar (app, menubar);
  g_object_unref (menubar);
  gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (win), FALSE);

  return GTK_WIDGET (win);
}
