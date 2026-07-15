#include "ooze-about-window.h"
#include "ooze-system-info.h"

#include "ooze-application-window.h"
#include "ooze-about.h"
#include "ooze-surface.h"

#include <adwaita.h>

struct _OozeAboutWindow
{
  OozeApplicationWindow parent_instance;
  GtkWidget *stack;
};

G_DEFINE_FINAL_TYPE (OozeAboutWindow, ooze_about_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

typedef struct
{
  const char *brand;
  const char *command;
  const char *desktop_id;
  const char *summary;
} OozeAppCatalogEntry;

static const OozeAppCatalogEntry ooze_apps[] = {
  { "Spot",         "spot",         "org.ooze.Spot",    "File manager" },
  { "Ooze Eye",     "ooze-eye",     "org.ooze.Eye",     "Image viewer" },
  { "Ooze Command", "ooze-command", "org.ooze.Command", "Terminal emulator" },
  { "Ooze King",    "ooze-king",    "org.ooze.King",    "System Settings" },
  { "Ooze Themes",  "ooze-themes",  "org.ooze.Themes",  "Appearance and theme settings" },
  { "Ooze Ear",     "ooze-ear",     "org.ooze.Ear",     "Sound Settings" },
  { "Ooze Pak",     "ooze-pak",     "org.ooze.Pak",     "Software installer" },
  { "Ooze About",   "ooze-about",   "org.ooze.About",   "About This Computer" },
};

static void
about_action_about (GSimpleAction *action G_GNUC_UNUSED,
                    GVariant      *param G_GNUC_UNUSED,
                    gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze About",
                      "help-about",
                      "System information and Ooze app versions.",
                      OOZE_VERSION);
}

static GMenuModel *
about_build_help_menu (void)
{
  GMenu *help;

  help = g_menu_new ();
  g_menu_append (help, "About Ooze About", "win.about");
  return G_MENU_MODEL (help);
}

static void
add_info_row (GtkWidget *list, const char *title, const char *value)
{
  GtkWidget *row;
  GtkWidget *suffix;

  row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

  suffix = gtk_label_new (value && *value ? value : "—");
  gtk_label_set_xalign (GTK_LABEL (suffix), 1.0);
  gtk_label_set_wrap (GTK_LABEL (suffix), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (suffix), 36);
  gtk_widget_set_opacity (suffix, 0.85);
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), suffix);

  gtk_list_box_append (GTK_LIST_BOX (list), row);
}

static GtkWidget *
build_system_page (void)
{
  GtkWidget *scrolled;
  GtkWidget *box;
  GtkWidget *list;
  GtkWidget *title;
  OozeSystemInfo *info;

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (box, 18);
  gtk_widget_set_margin_end (box, 18);
  gtk_widget_set_margin_top (box, 18);
  gtk_widget_set_margin_bottom (box, 18);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), box);

  title = gtk_label_new ("About This Computer");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_widget_add_css_class (title, "title-1");
  gtk_box_append (GTK_BOX (box), title);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (list, "boxed-list");
  gtk_box_append (GTK_BOX (box), list);

  info = ooze_system_info_gather ();
  add_info_row (list, "Device Name", info->device_name);
  add_info_row (list, "Hardware Model", info->hardware_model);
  add_info_row (list, "Operating System", info->operating_system);
  add_info_row (list, "Processor", info->processor);
  add_info_row (list, "Memory", info->memory);
  add_info_row (list, "Disk Capacity", info->disk_capacity);
  add_info_row (list, "Kernel", info->kernel);
  ooze_system_info_free (info);

  return scrolled;
}

static GtkWidget *
build_apps_page (void)
{
  GtkWidget *scrolled;
  GtkWidget *box;
  GtkWidget *list;
  GtkWidget *title;
  gsize i;

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (box, 18);
  gtk_widget_set_margin_end (box, 18);
  gtk_widget_set_margin_top (box, 18);
  gtk_widget_set_margin_bottom (box, 18);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), box);

  title = gtk_label_new ("Ooze Apps");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_widget_add_css_class (title, "title-1");
  gtk_box_append (GTK_BOX (box), title);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (list, "boxed-list");
  gtk_box_append (GTK_BOX (box), list);

  for (i = 0; i < G_N_ELEMENTS (ooze_apps); i++)
    {
      const OozeAppCatalogEntry *entry = &ooze_apps[i];
      GtkWidget *row;
      GtkWidget *suffix;
      g_autofree char *exe = NULL;
      g_autofree char *subtitle = NULL;
      gboolean installed;

      exe = g_find_program_in_path (entry->command);
      installed = exe != NULL;

      row = adw_action_row_new ();
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), entry->brand);
      subtitle = g_strdup_printf ("%s%s",
                                  entry->summary,
                                  installed ? "" : " — not installed");
      adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

      suffix = gtk_label_new (installed ? OOZE_VERSION : "—");
      gtk_widget_set_opacity (suffix, installed ? 0.85 : 0.45);
      adw_action_row_add_suffix (ADW_ACTION_ROW (row), suffix);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
    }

  return scrolled;
}

static void
on_nav_activated (GtkListBox *box G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  OozeAboutWindow *self)
{
  const char *id;

  if (!row)
    return;

  id = g_object_get_data (G_OBJECT (row), "page-id");
  if (id)
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), id);
}

static GtkWidget *
make_nav_row (const char *id, const char *label)
{
  GtkWidget *row;
  GtkWidget *lbl;

  row = gtk_list_box_row_new ();
  g_object_set_data (G_OBJECT (row), "page-id", (gpointer) id);
  lbl = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.0);
  gtk_widget_set_margin_start (lbl, 12);
  gtk_widget_set_margin_end (lbl, 12);
  gtk_widget_set_margin_top (lbl, 10);
  gtk_widget_set_margin_bottom (lbl, 10);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), lbl);
  return row;
}

static void ooze_about_window_constructed (GObject *object);

static void
ooze_about_window_class_init (OozeAboutWindowClass *klass G_GNUC_UNUSED)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_about_window_constructed;
}

static void
ooze_about_window_init (OozeAboutWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", about_action_about, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_about_window_constructed (GObject *object)
{
  OozeAboutWindow *self = OOZE_ABOUT_WINDOW (object);
  GtkWidget *shell;
  GtkWidget *surface;
  GtkWidget *split;
  GtkWidget *nav;
  GtkWidget *system_row;
  GMenuModel *help;

  G_OBJECT_CLASS (ooze_about_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 720, 480);
  gtk_window_set_icon_name (GTK_WINDOW (self), "help-about");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-about");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "About This Computer");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_box_append (GTK_BOX (shell), surface);

  split = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (split, TRUE);
  gtk_widget_set_vexpand (split, TRUE);
  gtk_box_append (GTK_BOX (surface), split);

  nav = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (nav), GTK_SELECTION_SINGLE);
  gtk_widget_set_size_request (nav, 180, -1);
  gtk_widget_add_css_class (nav, "navigation-sidebar");
  system_row = make_nav_row ("system", "System");
  gtk_list_box_append (GTK_LIST_BOX (nav), system_row);
  gtk_list_box_append (GTK_LIST_BOX (nav), make_nav_row ("apps", "Ooze Apps"));
  gtk_box_append (GTK_BOX (split), nav);

  self->stack = gtk_stack_new ();
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_stack_add_titled (GTK_STACK (self->stack), build_system_page (),
                        "system", "System");
  gtk_stack_add_titled (GTK_STACK (self->stack), build_apps_page (),
                        "apps", "Ooze Apps");
  gtk_box_append (GTK_BOX (split), self->stack);

  g_signal_connect (nav, "row-activated", G_CALLBACK (on_nav_activated), self);
  gtk_list_box_select_row (GTK_LIST_BOX (nav), GTK_LIST_BOX_ROW (system_row));

  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  help = about_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_about_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_ABOUT_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
