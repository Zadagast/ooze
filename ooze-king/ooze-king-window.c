#include "ooze-king-window.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-icons.h"
#include "ooze-shared-appmenu.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <gio/gio.h>
#include <string.h>

struct _OozeKingWindow
{
  OozeApplicationWindow parent_instance;
  GtkWidget *grid;
};

G_DEFINE_FINAL_TYPE (OozeKingWindow, ooze_king_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

typedef struct {
  const char * const *icons;
  const char         *label;
  const char         *command;
} KingAppEntry;

static const char * const king_icon_spot[] = {
  "system-file-manager", NULL
};
static const char * const king_icon_command[] = {
  "utilities-terminal", NULL
};
static const char * const king_icon_ear[] = {
  "audio-headphones", "multimedia-volume-control", NULL
};
static const char * const king_icon_monitor[] = {
  "video-display", "preferences-desktop-display", NULL
};
static const char * const king_icon_themes[] = {
  "preferences-desktop-theme", "preferences-desktop", NULL
};
static const char * const king_icon_about[] = {
  "help-about", "dialog-information", NULL
};
static const char * const king_icon_pak[] = {
  "system-software-install", "package-x-generic", NULL
};
static const char * const king_icon_torrent[] = {
  "application-x-bittorrent", "network-workgroup", NULL
};
static const char * const king_icon_mouse[] = {
  "input-mouse", "preferences-desktop-peripherals", NULL
};
static const char * const king_icon_defaults[] = {
  "preferences-desktop-default-applications", "application-x-executable", NULL
};

static const KingAppEntry king_apps[] = {
  { king_icon_about,   "About This Computer", "ooze-about" },
  { king_icon_monitor, "Displays",            "ooze-monitor" },
  { king_icon_themes,  "Themes",              "ooze-themes" },
  { king_icon_defaults, "Default Apps",       "ooze-defaults" },
  { king_icon_mouse,   "Mouse",               "ooze-mouse" },
  { king_icon_spot,    "Spot",                "spot" },
  { king_icon_command, "Terminal",       "ooze-command" },
  { king_icon_ear,     "Sound Settings", "ooze-ear" },
  { king_icon_pak,     "Software",       "ooze-pak" },
  { king_icon_torrent, "Torrent",        "ooze-torrent" },
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
king_build_help_menu (void)
{
  GMenu *help;

  help = g_menu_new ();
  g_menu_append (help, "About Ooze King", "win.about");
  return G_MENU_MODEL (help);
}

static gboolean
king_command_is_ooze (const char *command)
{
  if (!command || !*command)
    return FALSE;
  if (g_strcmp0 (command, "spot") == 0)
    return TRUE;
  return g_str_has_prefix (command, "ooze-");
}

static void
king_launch_command (const char *command)
{
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GAppLaunchContext) ctx = NULL;
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

  ctx = g_app_launch_context_new ();
  if (king_command_is_ooze (command))
    ooze_appmenu_prepare_ooze_launch_context (ctx);
  else
    ooze_appmenu_prepare_launch_context (ctx);

  if (!g_app_info_launch (info, NULL, ctx, &error))
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

static void ooze_king_window_constructed (GObject *object);

static void
ooze_king_window_class_init (OozeKingWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_king_window_constructed;
}

static void
ooze_king_window_init (OozeKingWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", king_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_king_window_constructed (GObject *object)
{
  OozeKingWindow *self = OOZE_KING_WINDOW (object);
  GMenuModel *help;
  GtkWidget *shell;
  GtkWidget *surface;
  gsize i;

  G_OBJECT_CLASS (ooze_king_window_parent_class)->constructed (object);

  ooze_toolbar_ensure_css ();

  gtk_window_set_default_size (GTK_WINDOW (self), 560, 420);
  gtk_window_set_icon_name (GTK_WINDOW (self), "preferences-system");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-king");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "System Settings");

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

  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  help = king_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_king_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_KING_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
