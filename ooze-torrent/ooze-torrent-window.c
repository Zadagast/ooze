#include "ooze-torrent-window.h"
#include "ooze-torrent-session.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <string.h>

struct _OozeTorrentWindow
{
  OozeApplicationWindow parent_instance;

  GtkWidget *list;
  GtkWidget *empty;
  GtkWidget *status_label;
  GtkWidget *stack;

  OozeTrSession *session;
  guint          refresh_id;
  int            selected_id;
};

G_DEFINE_FINAL_TYPE (OozeTorrentWindow, ooze_torrent_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void torrent_refresh (OozeTorrentWindow *self);
static void ooze_torrent_window_constructed (GObject *object);

static GtkWidget *
toolbar_btn (const char * const *icons,
             const char         *label,
             const char         *tooltip)
{
  return ooze_button_new_toolbar (icons, label, tooltip);
}

static void
torrent_action_about (GSimpleAction *action G_GNUC_UNUSED,
                      GVariant      *param G_GNUC_UNUSED,
                      gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Torrent",
                      "application-x-bittorrent",
                      "BitTorrent client for Ooze Desktop.",
                      OOZE_VERSION);
}

static GMenuModel *
torrent_build_file_menu (void)
{
  GMenu *file = g_menu_new ();
  g_menu_append (file, "Add Torrent…", "win.add-torrent");
  g_menu_append (file, "Add Magnet…", "win.add-magnet");
  g_menu_append (file, "Open Download Folder", "win.open-folder");
  return G_MENU_MODEL (file);
}

static GMenuModel *
torrent_build_help_menu (void)
{
  GMenu *help = g_menu_new ();

  g_menu_append (help, "About Ooze Torrent", "win.about");
  return G_MENU_MODEL (help);
}

static void
format_rate (char *buf, size_t buflen, double kbps)
{
  if (kbps < 0.05)
    g_snprintf (buf, buflen, "—");
  else if (kbps < 1024.0)
    g_snprintf (buf, buflen, "%.0f KB/s", kbps);
  else
    g_snprintf (buf, buflen, "%.1f MB/s", kbps / 1024.0);
}

static GtkWidget *
make_row (OozeTorrentWindow *self, const OozeTrTorrentInfo *info)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *top;
  GtkWidget *name;
  GtkWidget *bar;
  GtkWidget *meta;
  GtkWidget *state;
  GtkWidget *rates;
  char down[32], up[32], rate_line[80];

  row = gtk_list_box_row_new ();
  g_object_set_data (G_OBJECT (row), "torrent-id",
                     GINT_TO_POINTER (info->id));

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);

  top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  name = gtk_label_new (info->name ? info->name : "Torrent");
  gtk_label_set_xalign (GTK_LABEL (name), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (name, TRUE);
  gtk_box_append (GTK_BOX (top), name);

  state = gtk_label_new (ooze_tr_state_label (info->state));
  gtk_widget_add_css_class (state, "dim-label");
  gtk_box_append (GTK_BOX (top), state);
  gtk_box_append (GTK_BOX (box), top);

  bar = gtk_progress_bar_new ();
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (bar),
                                 CLAMP (info->progress, 0.0, 1.0));
  gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (bar), TRUE);
  {
    g_autofree char *pct = g_strdup_printf ("%.0f%%", info->progress * 100.0);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (bar), pct);
  }
  gtk_box_append (GTK_BOX (box), bar);

  format_rate (down, sizeof down, info->down_KBps);
  format_rate (up, sizeof up, info->up_KBps);
  g_snprintf (rate_line, sizeof rate_line, "↓ %s   ↑ %s", down, up);

  meta = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  rates = gtk_label_new (rate_line);
  gtk_label_set_xalign (GTK_LABEL (rates), 0.0);
  gtk_widget_add_css_class (rates, "dim-label");
  gtk_widget_set_hexpand (rates, TRUE);
  gtk_box_append (GTK_BOX (meta), rates);
  gtk_box_append (GTK_BOX (box), meta);

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

  if (info->id == self->selected_id)
    gtk_list_box_select_row (GTK_LIST_BOX (self->list), GTK_LIST_BOX_ROW (row));

  return row;
}

static void
clear_list (GtkListBox *list)
{
  GtkListBoxRow *row;

  while ((row = gtk_list_box_get_row_at_index (list, 0)))
    gtk_list_box_remove (list, GTK_WIDGET (row));
}

static void
torrent_refresh (OozeTorrentWindow *self)
{
  guint n = 0;
  OozeTrTorrentInfo *infos;
  guint i;
  guint active = 0;
  char status[128];

  if (!self->session)
    return;

  infos = ooze_tr_session_list (self->session, &n);
  clear_list (GTK_LIST_BOX (self->list));

  for (i = 0; i < n; i++)
    {
      gtk_list_box_append (GTK_LIST_BOX (self->list), make_row (self, &infos[i]));
      if (infos[i].state == OOZE_TR_STATE_DOWNLOAD ||
          infos[i].state == OOZE_TR_STATE_SEED)
        active++;
    }

  if (n == 0)
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
  else
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "list");

  g_snprintf (status, sizeof status, "%u torrent%s · %u active",
              n, n == 1 ? "" : "s", active);
  gtk_label_set_text (GTK_LABEL (self->status_label), status);

  ooze_tr_session_free_list (infos, n);
}

static gboolean
on_refresh_tick (gpointer user_data)
{
  torrent_refresh (OOZE_TORRENT_WINDOW (user_data));
  return G_SOURCE_CONTINUE;
}

static int
selected_id (OozeTorrentWindow *self)
{
  GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list));
  if (!row)
    return self->selected_id;
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "torrent-id"));
}

static void
on_row_selected (GtkListBox *box G_GNUC_UNUSED,
                 GtkListBoxRow *row,
                 gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  if (row)
    self->selected_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row),
                                                            "torrent-id"));
}

static void
on_add_torrent_response (GObject *source, GAsyncResult *res, gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file = NULL;
  g_autofree char *path = NULL;

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), res, &error);
  if (!file)
    return;

  path = g_file_get_path (file);
  if (!path)
    return;

  if (ooze_tr_session_add_file (self->session, path, &error) < 0 && error)
    {
      g_warning ("Ooze Torrent: %s", error->message);
      return;
    }
  torrent_refresh (self);
}

static void
action_add_torrent (GSimpleAction *a G_GNUC_UNUSED,
                    GVariant *p G_GNUC_UNUSED,
                    gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  GtkFileDialog *dialog;
  g_autoptr (GtkFileFilter) filter = NULL;
  g_autoptr (GListStore) filters = NULL;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Add Torrent");

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Torrent files");
  gtk_file_filter_add_pattern (filter, "*.torrent");
  gtk_file_filter_add_mime_type (filter, "application/x-bittorrent");
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL,
                        on_add_torrent_response, self);
  g_object_unref (dialog);
}

static void
magnet_dialog_finish (GtkWindow *dialog, gboolean accept, gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  GtkWidget *entry;
  const char *text;
  g_autoptr (GError) error = NULL;

  if (accept)
    {
      entry = g_object_get_data (G_OBJECT (dialog), "entry");
      text = gtk_editable_get_text (GTK_EDITABLE (entry));
      if (text && *text)
        {
          if (ooze_tr_session_add_magnet (self->session, text, &error) < 0 && error)
            g_warning ("Ooze Torrent: %s", error->message);
          torrent_refresh (self);
        }
    }
  gtk_window_destroy (dialog);
}

static void
on_magnet_cancel (GtkButton *btn G_GNUC_UNUSED, gpointer user_data)
{
  GtkWindow *dialog = GTK_WINDOW (user_data);
  magnet_dialog_finish (dialog, FALSE,
                        g_object_get_data (G_OBJECT (dialog), "owner"));
}

static void
on_magnet_add (GtkButton *btn G_GNUC_UNUSED, gpointer user_data)
{
  GtkWindow *dialog = GTK_WINDOW (user_data);
  magnet_dialog_finish (dialog, TRUE,
                        g_object_get_data (G_OBJECT (dialog), "owner"));
}

static void
action_add_magnet (GSimpleAction *a G_GNUC_UNUSED,
                   GVariant *p G_GNUC_UNUSED,
                   gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  GtkWidget *dialog;
  GtkWidget *box;
  GtkWidget *entry;
  GtkWidget *buttons;
  GtkWidget *cancel;
  GtkWidget *add;

  dialog = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (dialog), "Add Magnet Link");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (self));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 480, -1);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 16);
  gtk_widget_set_margin_bottom (box, 16);

  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "magnet:?xt=urn:btih:…");
  gtk_box_append (GTK_BOX (box), entry);

  buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign (buttons, GTK_ALIGN_END);
  cancel = gtk_button_new_with_label ("Cancel");
  add = gtk_button_new_with_label ("Add");
  gtk_widget_add_css_class (add, "suggested-action");
  gtk_box_append (GTK_BOX (buttons), cancel);
  gtk_box_append (GTK_BOX (buttons), add);
  gtk_box_append (GTK_BOX (box), buttons);

  gtk_window_set_child (GTK_WINDOW (dialog), box);
  g_object_set_data (G_OBJECT (dialog), "entry", entry);
  g_object_set_data (G_OBJECT (dialog), "owner", self);
  g_signal_connect (cancel, "clicked", G_CALLBACK (on_magnet_cancel), dialog);
  g_signal_connect (add, "clicked", G_CALLBACK (on_magnet_add), dialog);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
action_pause (GSimpleAction *a G_GNUC_UNUSED,
              GVariant *p G_GNUC_UNUSED,
              gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  int id = selected_id (self);
  if (id > 0)
    ooze_tr_session_stop (self->session, id);
  torrent_refresh (self);
}

static void
action_resume (GSimpleAction *a G_GNUC_UNUSED,
               GVariant *p G_GNUC_UNUSED,
               gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  int id = selected_id (self);
  if (id > 0)
    ooze_tr_session_start (self->session, id);
  torrent_refresh (self);
}

static void
action_remove (GSimpleAction *a G_GNUC_UNUSED,
               GVariant *p G_GNUC_UNUSED,
               gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  int id = selected_id (self);
  if (id > 0)
    {
      ooze_tr_session_remove (self->session, id, false);
      self->selected_id = 0;
    }
  torrent_refresh (self);
}

static void
action_open_folder (GSimpleAction *a G_GNUC_UNUSED,
                    GVariant *p G_GNUC_UNUSED,
                    gpointer user_data)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (user_data);
  guint n = 0;
  OozeTrTorrentInfo *infos;
  int id = selected_id (self);
  guint i;
  const char *dir = NULL;

  infos = ooze_tr_session_list (self->session, &n);
  for (i = 0; i < n; i++)
    {
      if (infos[i].id == id)
        {
          dir = infos[i].download_dir;
          break;
        }
    }
  if (!dir && n > 0)
    dir = infos[0].download_dir;

  if (dir && *dir)
    {
      g_autoptr (GFile) file = g_file_new_for_path (dir);
      g_autofree char *uri = g_file_get_uri (file);
      g_autoptr (GError) error = NULL;
      if (uri)
        g_app_info_launch_default_for_uri (uri, NULL, &error);
    }

  ooze_tr_session_free_list (infos, n);
}

static void
on_tb_add_torrent (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_add_torrent (NULL, NULL, ud);
}

static void
on_tb_add_magnet (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_add_magnet (NULL, NULL, ud);
}

static void
on_tb_pause (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_pause (NULL, NULL, ud);
}

static void
on_tb_resume (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_resume (NULL, NULL, ud);
}

static void
on_tb_remove (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_remove (NULL, NULL, ud);
}

static void
on_tb_open_folder (GtkButton *b G_GNUC_UNUSED, gpointer ud)
{
  action_open_folder (NULL, NULL, ud);
}

static GtkWidget *
build_toolbar (OozeTorrentWindow *self)
{
  GtkWidget *toolbar;
  GtkWidget *group;
  GtkWidget *btn;
  static const char * const icon_add[] = { "list-add", NULL };
  static const char * const icon_magnet[] = { "insert-link", "emblem-symbolic-link", NULL };
  static const char * const icon_pause[] = { "media-playback-pause", NULL };
  static const char * const icon_resume[] = { "media-playback-start", NULL };
  static const char * const icon_remove[] = { "list-remove", "edit-delete", NULL };
  static const char * const icon_folder[] = { "folder-open", "document-open", NULL };

  toolbar = ooze_toolbar_new ();
  group = ooze_toolbar_add_group (toolbar);

  btn = toolbar_btn (icon_add, "Add", "Add Torrent");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_add_torrent), self);
  gtk_box_append (GTK_BOX (group), btn);

  btn = toolbar_btn (icon_magnet, "Magnet", "Add Magnet");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_add_magnet), self);
  gtk_box_append (GTK_BOX (group), btn);

  ooze_toolbar_add_separator (toolbar);
  group = ooze_toolbar_add_group (toolbar);

  btn = toolbar_btn (icon_pause, "Pause", "Pause");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_pause), self);
  gtk_box_append (GTK_BOX (group), btn);

  btn = toolbar_btn (icon_resume, "Resume", "Resume");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_resume), self);
  gtk_box_append (GTK_BOX (group), btn);

  btn = toolbar_btn (icon_remove, "Remove", "Remove");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_remove), self);
  gtk_box_append (GTK_BOX (group), btn);

  ooze_toolbar_add_separator (toolbar);
  group = ooze_toolbar_add_group (toolbar);

  btn = toolbar_btn (icon_folder, "Folder", "Open Download Folder");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tb_open_folder), self);
  gtk_box_append (GTK_BOX (group), btn);

  return toolbar;
}

static void
ooze_torrent_window_dispose (GObject *object)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (object);

  if (self->refresh_id)
    {
      g_source_remove (self->refresh_id);
      self->refresh_id = 0;
    }
  g_clear_pointer (&self->session, ooze_tr_session_free);

  G_OBJECT_CLASS (ooze_torrent_window_parent_class)->dispose (object);
}

static void
ooze_torrent_window_class_init (OozeTorrentWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->constructed = ooze_torrent_window_constructed;
  object_class->dispose = ooze_torrent_window_dispose;
}

static void
ooze_torrent_window_init (OozeTorrentWindow *self)
{
  static const GActionEntry entries[] = {
    { "about", torrent_action_about, NULL, NULL, NULL },
    { "add-torrent", action_add_torrent, NULL, NULL, NULL },
    { "add-magnet", action_add_magnet, NULL, NULL, NULL },
    { "pause", action_pause, NULL, NULL, NULL },
    { "resume", action_resume, NULL, NULL, NULL },
    { "remove", action_remove, NULL, NULL, NULL },
    { "open-folder", action_open_folder, NULL, NULL, NULL },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_torrent_window_constructed (GObject *object)
{
  OozeTorrentWindow *self = OOZE_TORRENT_WINDOW (object);
  GMenuModel *file;
  GMenuModel *help;
  GtkWidget *shell;
  GtkWidget *toolbar;
  GtkWidget *scrolled;
  GtkWidget *statusbar;
  GtkWidget *empty_box;
  GtkWidget *empty_label;
  g_autoptr (GError) error = NULL;

  G_OBJECT_CLASS (ooze_torrent_window_parent_class)->constructed (object);

  gtk_window_set_default_size (GTK_WINDOW (self), 720, 480);
  gtk_window_set_icon_name (GTK_WINDOW (self), "application-x-bittorrent");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-torrent");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Ooze Torrent");

  self->session = ooze_tr_session_new (&error);
  if (!self->session)
    g_warning ("Ooze Torrent: %s", error ? error->message : "session failed");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  toolbar = build_toolbar (self);
  gtk_box_append (GTK_BOX (shell), toolbar);

  self->stack = gtk_stack_new ();
  gtk_widget_set_vexpand (self->stack, TRUE);

  empty_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_valign (empty_box, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (empty_box, GTK_ALIGN_CENTER);
  empty_label = gtk_label_new ("No torrents yet.\nAdd a torrent file or magnet link to begin.");
  gtk_label_set_justify (GTK_LABEL (empty_label), GTK_JUSTIFY_CENTER);
  gtk_widget_add_css_class (empty_label, "dim-label");
  gtk_box_append (GTK_BOX (empty_box), empty_label);
  self->empty = empty_box;
  gtk_stack_add_named (GTK_STACK (self->stack), empty_box, "empty");

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  self->list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->list),
                                   GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class (self->list, "rich-list");
  g_signal_connect (self->list, "row-selected", G_CALLBACK (on_row_selected), self);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), self->list);
  gtk_stack_add_named (GTK_STACK (self->stack), scrolled, "list");

  gtk_box_append (GTK_BOX (shell), self->stack);

  statusbar = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
  self->status_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
  gtk_widget_set_margin_start (self->status_label, 10);
  gtk_box_append (GTK_BOX (statusbar), self->status_label);
  gtk_box_append (GTK_BOX (shell), statusbar);

  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  torrent_refresh (self);
  self->refresh_id = g_timeout_add_seconds (1, on_refresh_tick, self);

  file = torrent_build_file_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "File", file);
  g_object_unref (file);
  help = torrent_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_torrent_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_TORRENT_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}

void
ooze_torrent_window_open_file (OozeTorrentWindow *self, GFile *file)
{
  g_autofree char *path = NULL;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (OOZE_TORRENT_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  path = g_file_get_path (file);
  if (!path || !self->session)
    return;

  if (ooze_tr_session_add_file (self->session, path, &error) < 0 && error)
    g_warning ("Ooze Torrent: %s", error->message);
  torrent_refresh (self);
}

void
ooze_torrent_window_open_uri (OozeTorrentWindow *self, const char *uri)
{
  g_autoptr (GError) error = NULL;

  g_return_if_fail (OOZE_TORRENT_IS_WINDOW (self));
  g_return_if_fail (uri != NULL);

  if (g_str_has_prefix (uri, "magnet:"))
    {
      if (ooze_tr_session_add_magnet (self->session, uri, &error) < 0 && error)
        g_warning ("Ooze Torrent: %s", error->message);
      torrent_refresh (self);
      return;
    }

  {
    g_autoptr (GFile) file = g_file_new_for_uri (uri);
    ooze_torrent_window_open_file (self, file);
  }
}
