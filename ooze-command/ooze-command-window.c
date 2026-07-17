#include "ooze-command-window.h"

#include "ooze-shared-appmenu.h"
#include "ooze-button.h"
#include "ooze-icons.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"
#include "ooze-about.h"

#include <vte/vte.h>
#include <pango/pango.h>

/* ── Dark terminal colour palette (Ooze identity) ───────────────────────── */
#define OC_BG_R   0.110   /* #1c1c1e */
#define OC_BG_G   0.110
#define OC_BG_B   0.118

#define OC_FG_R   0.878   /* #e0e0e0 */
#define OC_FG_G   0.878
#define OC_FG_B   0.878

static const GdkRGBA oc_palette[16] = {
  { 0.137, 0.137, 0.145, 1.0 },
  { 0.902, 0.235, 0.235, 1.0 },
  { 0.188, 0.749, 0.376, 1.0 },
  { 0.902, 0.718, 0.235, 1.0 },
  { 0.196, 0.467, 0.843, 1.0 },
  { 0.686, 0.259, 0.863, 1.0 },
  { 0.188, 0.749, 0.749, 1.0 },
  { 0.784, 0.784, 0.800, 1.0 },
  { 0.376, 0.376, 0.408, 1.0 },
  { 1.000, 0.427, 0.427, 1.0 },
  { 0.349, 0.898, 0.541, 1.0 },
  { 1.000, 0.855, 0.431, 1.0 },
  { 0.329, 0.588, 1.000, 1.0 },
  { 0.847, 0.435, 1.000, 1.0 },
  { 0.349, 0.898, 0.898, 1.0 },
  { 0.941, 0.941, 0.941, 1.0 },
};

typedef struct _OozeCommandTab OozeCommandTab;

struct _OozeCommandTab
{
  OozeCommandWindow *window;
  char              *name;       /* stack child name */
  GtkWidget         *page;       /* scrolled window */
  GtkWidget         *terminal;
  GtkWidget         *chip;       /* sidebar list row */
  GtkWidget         *title_label;
  guint              id;
  guint              close_idle_id;
  gboolean           closing;
};

struct _OozeCommandWindow
{
  OozeApplicationWindow parent_instance;

  GtkWidget *sidebar;
  GtkWidget *tab_list;
  GtkWidget *new_tab_button;
  GtkWidget *close_tab_button;
  GtkWidget *stack;

  GList            *tabs;
  OozeCommandTab   *active_tab;
  guint             next_tab_id;
  double            font_scale;
  gulong            tab_select_handler;
};

G_DEFINE_FINAL_TYPE (OozeCommandWindow, ooze_command_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static const char * const oc_icon_new_tab[] = {
  "list-add", "tab-new-symbolic", "list-add-symbolic", NULL
};

static const char * const oc_icon_close_tab[] = {
  "list-remove", "tab-close-symbolic", "window-close-symbolic", "list-remove-symbolic", NULL
};

static const char * const oc_icon_terminal[] = {
  "utilities-terminal",
  "org.gnome.Terminal",
  "terminal",
  "utilities-terminal-symbolic",
  NULL
};

static void oc_select_tab (OozeCommandWindow *self, OozeCommandTab *tab);
static void oc_close_tab  (OozeCommandWindow *self, OozeCommandTab *tab);
static OozeCommandTab *oc_add_tab (OozeCommandWindow *self, gboolean select);
static void oc_apply_font_to_terminal (OozeCommandWindow *self, VteTerminal *vte);
static void oc_update_window_title (OozeCommandWindow *self);
static void ooze_command_window_constructed (GObject *object);

static void
oc_apply_colors (VteTerminal *vte)
{
  const GdkRGBA bg     = { OC_BG_R, OC_BG_G, OC_BG_B, 1.0 };
  const GdkRGBA fg     = { OC_FG_R, OC_FG_G, OC_FG_B, 1.0 };
  const GdkRGBA cursor = { 0.196, 0.467, 0.843, 1.0 };

  vte_terminal_set_colors (vte, &fg, &bg, oc_palette, G_N_ELEMENTS (oc_palette));
  vte_terminal_set_color_cursor (vte, &cursor);
  vte_terminal_set_color_highlight (vte, &(GdkRGBA){ 0.196, 0.467, 0.843, 0.35 });
}

static void
oc_apply_font_to_terminal (OozeCommandWindow *self, VteTerminal *vte)
{
  PangoFontDescription *fd = pango_font_description_from_string ("Monospace 11");

  vte_terminal_set_font (vte, fd);
  vte_terminal_set_font_scale (vte, self->font_scale);
  pango_font_description_free (fd);
}

static void
oc_apply_font (OozeCommandWindow *self)
{
  GList *l;

  for (l = self->tabs; l != NULL; l = l->next)
    {
      OozeCommandTab *tab = l->data;
      oc_apply_font_to_terminal (self, VTE_TERMINAL (tab->terminal));
    }
}

static void
oc_update_window_title (OozeCommandWindow *self)
{
  const char *title = "Terminal";

  if (self->active_tab)
    {
      const char *wt =
        vte_terminal_get_window_title (VTE_TERMINAL (self->active_tab->terminal));
      if (wt && *wt)
        title = wt;
      else if (self->active_tab->title_label)
        title = gtk_label_get_text (GTK_LABEL (self->active_tab->title_label));
    }

  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), title);
}

static void
oc_refresh_tab_chips (OozeCommandWindow *self)
{
  if (!self->tab_list || !self->active_tab || !self->active_tab->chip)
    return;

  if (self->tab_select_handler)
    g_signal_handler_block (self->tab_list, self->tab_select_handler);
  gtk_list_box_select_row (GTK_LIST_BOX (self->tab_list),
                           GTK_LIST_BOX_ROW (self->active_tab->chip));
  if (self->tab_select_handler)
    g_signal_handler_unblock (self->tab_list, self->tab_select_handler);
}

static void
oc_select_tab (OozeCommandWindow *self, OozeCommandTab *tab)
{
  if (!self || !tab)
    return;

  self->active_tab = tab;
  gtk_stack_set_visible_child (GTK_STACK (self->stack), tab->page);
  oc_refresh_tab_chips (self);
  oc_update_window_title (self);
  gtk_widget_grab_focus (tab->terminal);
}

static void
oc_tab_free (OozeCommandTab *tab)
{
  if (!tab)
    return;
  if (tab->close_idle_id)
    {
      g_source_remove (tab->close_idle_id);
      tab->close_idle_id = 0;
    }
  g_free (tab->name);
  g_free (tab);
}

static void
on_title_changed (GObject        *object,
                  GParamSpec     *pspec G_GNUC_UNUSED,
                  OozeCommandTab *tab)
{
  VteTerminal *vte = VTE_TERMINAL (object);
  const char *title;

  if (!tab || !GTK_IS_LABEL (tab->title_label))
    return;

  title = vte_terminal_get_window_title (vte);
  if (title && *title)
    gtk_label_set_text (GTK_LABEL (tab->title_label), title);
  else
    gtk_label_set_text (GTK_LABEL (tab->title_label), "Terminal");

  if (tab->window && tab->window->active_tab == tab)
    oc_update_window_title (tab->window);
}

static gboolean
oc_close_tab_idle (gpointer user_data)
{
  OozeCommandTab *tab = user_data;

  tab->close_idle_id = 0;
  if (tab->window)
    oc_close_tab (tab->window, tab);
  return G_SOURCE_REMOVE;
}

static void
on_child_exited (VteTerminal *vte G_GNUC_UNUSED,
                 int          status G_GNUC_UNUSED,
                 OozeCommandTab *tab)
{
  /* Defer so we don't destroy the terminal from inside its own signal. */
  if (!tab || tab->close_idle_id)
    return;
  tab->close_idle_id = g_idle_add (oc_close_tab_idle, tab);
}

static void
oc_spawn_shell (VteTerminal *vte)
{
  const char  *shell;
  const char  *argv[2];
  char       **envp;

  shell = vte_get_user_shell ();
  if (!shell || !*shell)
    shell = "/bin/bash";

  argv[0] = shell;
  argv[1] = NULL;
  envp = ooze_appmenu_environ_for_foreign (g_get_environ ());

  vte_terminal_spawn_async (vte,
                            VTE_PTY_DEFAULT,
                            NULL,
                            (char **) argv,
                            envp,
                            G_SPAWN_SEARCH_PATH,
                            NULL, NULL, NULL,
                            -1,
                            NULL,
                            NULL, NULL);
  g_strfreev (envp);
}

static void
on_tab_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                      GtkListBoxRow *row,
                      OozeCommandWindow *self G_GNUC_UNUSED)
{
  OozeCommandTab *tab;

  if (!row)
    return;

  tab = g_object_get_data (G_OBJECT (row), "tab");
  if (tab && tab->window)
    oc_select_tab (tab->window, tab);
}

static void
on_tab_row_selected (GtkListBox    *box G_GNUC_UNUSED,
                     GtkListBoxRow *row,
                     OozeCommandWindow *self)
{
  OozeCommandTab *tab;

  if (!row)
    return;

  tab = g_object_get_data (G_OBJECT (row), "tab");
  if (tab && tab->window && tab != self->active_tab)
    oc_select_tab (tab->window, tab);
}

static GtkWidget *
oc_make_tab_chip (OozeCommandTab *tab)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *image;
  g_autofree char *label = NULL;

  row = gtk_list_box_row_new ();
  gtk_widget_add_css_class (row, "ooze-command-tab");
  g_object_set_data (G_OBJECT (row), "tab", tab);

  /* Same icon-above-label tile language as New Tab / Close Tab. */
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (box, 2);
  gtk_widget_set_margin_bottom (box, 2);

  image = ooze_icon_image_new (oc_icon_terminal, OOZE_ICON_SIZE_SIDEBAR);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), image);

  label = g_strdup_printf ("Terminal %u", tab->id);
  tab->title_label = gtk_label_new (label);
  gtk_label_set_ellipsize (GTK_LABEL (tab->title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (tab->title_label), 10);
  gtk_label_set_xalign (GTK_LABEL (tab->title_label), 0.5);
  gtk_widget_add_css_class (tab->title_label, "ooze-command-tab-label");
  gtk_box_append (GTK_BOX (box), tab->title_label);

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
  return row;
}

static OozeCommandTab *
oc_add_tab (OozeCommandWindow *self, gboolean select)
{
  OozeCommandTab *tab;
  GtkWidget *scrolled;
  GMenu *ctx;

  tab = g_new0 (OozeCommandTab, 1);
  tab->window = self;
  tab->id = self->next_tab_id++;
  tab->name = g_strdup_printf ("tab-%u", tab->id);

  tab->terminal = vte_terminal_new ();
  vte_terminal_set_audible_bell (VTE_TERMINAL (tab->terminal), FALSE);
  vte_terminal_set_scroll_on_keystroke (VTE_TERMINAL (tab->terminal), TRUE);
  vte_terminal_set_scroll_on_output (VTE_TERMINAL (tab->terminal), FALSE);
  vte_terminal_set_scrollback_lines (VTE_TERMINAL (tab->terminal), 10000);
  vte_terminal_set_cursor_blink_mode (VTE_TERMINAL (tab->terminal),
                                      VTE_CURSOR_BLINK_OFF);
  vte_terminal_set_cursor_shape (VTE_TERMINAL (tab->terminal),
                                 VTE_CURSOR_SHAPE_BLOCK);
  vte_terminal_set_bold_is_bright (VTE_TERMINAL (tab->terminal), TRUE);
  vte_terminal_set_allow_hyperlink (VTE_TERMINAL (tab->terminal), TRUE);

  oc_apply_colors (VTE_TERMINAL (tab->terminal));
  oc_apply_font_to_terminal (self, VTE_TERMINAL (tab->terminal));

  ctx = g_menu_new ();
  g_menu_append (ctx, "Copy", "win.copy");
  g_menu_append (ctx, "Paste", "win.paste");
  g_menu_append (ctx, "Select All", "win.select-all");
  vte_terminal_set_context_menu_model (VTE_TERMINAL (tab->terminal),
                                       G_MENU_MODEL (ctx));
  g_object_unref (ctx);

  g_signal_connect (tab->terminal, "notify::window-title",
                    G_CALLBACK (on_title_changed), tab);
  g_signal_connect (tab->terminal, "child-exited",
                    G_CALLBACK (on_child_exited), tab);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (tab->terminal, TRUE);
  gtk_widget_set_vexpand (tab->terminal, TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), tab->terminal);
  tab->page = scrolled;

  gtk_stack_add_named (GTK_STACK (self->stack), tab->page, tab->name);

  tab->chip = oc_make_tab_chip (tab);
  gtk_list_box_append (GTK_LIST_BOX (self->tab_list), tab->chip);

  self->tabs = g_list_append (self->tabs, tab);
  oc_spawn_shell (VTE_TERMINAL (tab->terminal));

  if (select)
    oc_select_tab (self, tab);

  return tab;
}

static void
oc_close_tab (OozeCommandWindow *self, OozeCommandTab *tab)
{
  GList *link;
  OozeCommandTab *next = NULL;

  if (!self || !tab || tab->closing)
    return;

  link = g_list_find (self->tabs, tab);
  if (!link)
    return;

  tab->closing = TRUE;

  if (link->next)
    next = link->next->data;
  else if (link->prev)
    next = link->prev->data;

  self->tabs = g_list_delete_link (self->tabs, link);

  if (self->active_tab == tab)
    self->active_tab = NULL;

  /* Disconnect before destroying VTE so child-exited cannot re-enter. */
  if (tab->terminal)
    g_signal_handlers_disconnect_by_data (tab->terminal, tab);

  if (tab->chip && self->tab_list)
    {
      g_object_set_data (G_OBJECT (tab->chip), "tab", NULL);
      gtk_list_box_remove (GTK_LIST_BOX (self->tab_list), tab->chip);
      tab->chip = NULL;
    }
  if (tab->page && self->stack)
    {
      gtk_stack_remove (GTK_STACK (self->stack), tab->page);
      tab->page = NULL;
      tab->terminal = NULL;
    }

  oc_tab_free (tab);

  if (!self->tabs)
    {
      gtk_window_destroy (GTK_WINDOW (self));
      return;
    }

  if (next)
    oc_select_tab (self, next);
}

static VteTerminal *
oc_active_vte (OozeCommandWindow *self)
{
  if (!self->active_tab)
    return NULL;
  return VTE_TERMINAL (self->active_tab->terminal);
}

static void
action_copy (GSimpleAction *a G_GNUC_UNUSED,
             GVariant      *p G_GNUC_UNUSED,
             gpointer       ud)
{
  VteTerminal *vte = oc_active_vte (OOZE_COMMAND_WINDOW (ud));
  if (vte && vte_terminal_get_has_selection (vte))
    vte_terminal_copy_clipboard_format (vte, VTE_FORMAT_TEXT);
}

static void
action_paste (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  VteTerminal *vte = oc_active_vte (OOZE_COMMAND_WINDOW (ud));
  if (vte)
    vte_terminal_paste_clipboard (vte);
}

static void
action_select_all (GSimpleAction *a G_GNUC_UNUSED,
                   GVariant      *p G_GNUC_UNUSED,
                   gpointer       ud)
{
  VteTerminal *vte = oc_active_vte (OOZE_COMMAND_WINDOW (ud));
  if (vte)
    vte_terminal_select_all (vte);
}

static void
action_clear (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  VteTerminal *vte = oc_active_vte (OOZE_COMMAND_WINDOW (ud));
  if (!vte)
    return;
  vte_terminal_set_scrollback_lines (vte, 0);
  vte_terminal_set_scrollback_lines (vte, 10000);
}

static void
action_zoom_in (GSimpleAction *a G_GNUC_UNUSED,
                GVariant      *p G_GNUC_UNUSED,
                gpointer       ud)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (ud);
  self->font_scale = MIN (self->font_scale * 1.1, 4.0);
  oc_apply_font (self);
}

static void
action_zoom_out (GSimpleAction *a G_GNUC_UNUSED,
                 GVariant      *p G_GNUC_UNUSED,
                 gpointer       ud)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (ud);
  self->font_scale = MAX (self->font_scale / 1.1, 0.25);
  oc_apply_font (self);
}

static void
action_zoom_reset (GSimpleAction *a G_GNUC_UNUSED,
                   GVariant      *p G_GNUC_UNUSED,
                   gpointer       ud)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (ud);
  self->font_scale = 1.0;
  oc_apply_font (self);
}

static void
action_new_window (GSimpleAction *a G_GNUC_UNUSED,
                   GVariant      *p G_GNUC_UNUSED,
                   gpointer       ud)
{
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (ud));
  GtkWidget *win = ooze_command_window_new (app);

  gtk_application_add_window (app, GTK_WINDOW (win));
  gtk_window_present (GTK_WINDOW (win));
}

static void
action_new_tab (GSimpleAction *a G_GNUC_UNUSED,
                GVariant      *p G_GNUC_UNUSED,
                gpointer       ud)
{
  oc_add_tab (OOZE_COMMAND_WINDOW (ud), TRUE);
}

static void
action_close (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (ud);

  if (self->active_tab)
    oc_close_tab (self, self->active_tab);
  else
    gtk_window_close (GTK_WINDOW (self));
}

static void
action_close_window (GSimpleAction *a G_GNUC_UNUSED,
                     GVariant      *p G_GNUC_UNUSED,
                     gpointer       ud)
{
  gtk_window_close (GTK_WINDOW (ud));
}

static void
action_minimize (GSimpleAction *a G_GNUC_UNUSED,
                 GVariant      *p G_GNUC_UNUSED,
                 gpointer       ud)
{
  gtk_window_minimize (GTK_WINDOW (ud));
}

static void
action_maximize (GSimpleAction *a G_GNUC_UNUSED,
                 GVariant      *p G_GNUC_UNUSED,
                 gpointer       ud)
{
  GtkWindow *win = GTK_WINDOW (ud);

  if (gtk_window_is_maximized (win))
    gtk_window_unmaximize (win);
  else
    gtk_window_maximize (win);
}

static void
action_about (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Command",
                      "utilities-terminal",
                      "Terminal emulator for Ooze Desktop.",
                      OOZE_VERSION);
}

static const GActionEntry win_actions[] = {
  { .name = "copy",         .activate = action_copy },
  { .name = "paste",        .activate = action_paste },
  { .name = "select-all",   .activate = action_select_all },
  { .name = "clear",        .activate = action_clear },
  { .name = "zoom-in",      .activate = action_zoom_in },
  { .name = "zoom-out",     .activate = action_zoom_out },
  { .name = "zoom-reset",   .activate = action_zoom_reset },
  { .name = "new-window",   .activate = action_new_window },
  { .name = "new-tab",      .activate = action_new_tab },
  { .name = "close",        .activate = action_close },
  { .name = "close-window", .activate = action_close_window },
  { .name = "minimize",     .activate = action_minimize },
  { .name = "maximize",     .activate = action_maximize },
  { .name = "about",        .activate = action_about },
};

static void
oc_add_shortcuts (OozeCommandWindow *self)
{
  struct { const char *action; const char *accel; } map[] = {
    { "win.copy",       "<Shift><Control>c" },
    { "win.paste",      "<Shift><Control>v" },
    { "win.zoom-in",    "<Control>equal"    },
    { "win.zoom-out",   "<Control>minus"    },
    { "win.zoom-reset", "<Control>0"        },
    { "win.new-window", "<Control>n"        },
    { "win.new-tab",    "<Shift><Control>t" },
    { "win.close",      "<Control>w"        },
  };
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (map); i++)
    {
      const char *accels[] = { map[i].accel, NULL };
      gtk_application_set_accels_for_action (app, map[i].action, accels);
    }
}

static void
oc_append_menus (OozeCommandWindow *self)
{
  GMenu *file, *edit, *view, *window, *help;

  file = g_menu_new ();
  g_menu_append (file, "New Tab",     "win.new-tab");
  g_menu_append (file, "New Window",  "win.new-window");
  g_menu_append (file, "Close Tab",   "win.close");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "File", G_MENU_MODEL (file));
  g_object_unref (file);

  edit = g_menu_new ();
  g_menu_append (edit, "Copy",        "win.copy");
  g_menu_append (edit, "Paste",       "win.paste");
  g_menu_append (edit, "Select All",  "win.select-all");
  g_menu_append (edit, "Clear Scrollback", "win.clear");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Edit", G_MENU_MODEL (edit));
  g_object_unref (edit);

  view = g_menu_new ();
  g_menu_append (view, "Zoom In",     "win.zoom-in");
  g_menu_append (view, "Zoom Out",    "win.zoom-out");
  g_menu_append (view, "Normal Size", "win.zoom-reset");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "View", G_MENU_MODEL (view));
  g_object_unref (view);

  window = g_menu_new ();
  g_menu_append (window, "Minimize",     "win.minimize");
  g_menu_append (window, "Maximize",     "win.maximize");
  g_menu_append (window, "Close Window", "win.close-window");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Window", G_MENU_MODEL (window));
  g_object_unref (window);

  help = g_menu_new ();
  g_menu_append (help, "About Ooze Command", "win.about");
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", G_MENU_MODEL (help));
  g_object_unref (help);
}

static void
oc_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay     *display;
  GtkCssProvider *provider;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider,
    "window.csd.ooze-command > decoration {"
    "  border-radius: 9px;"
    "  box-shadow:"
    "    0 2px  6px rgba(0,0,0,0.28),"
    "    0 8px 24px rgba(0,0,0,0.50),"
    "    0 20px 44px rgba(0,0,0,0.28);"
    "}"
    "vte-terminal {"
    "  min-width:  480px;"
    "  min-height: 280px;"
    "}"
    ".ooze-command-sidebar-list { background: none; }"
    ".ooze-command-sidebar-list row { padding: 5px 6px; }"
    ".ooze-command-sidebar-list row:hover {"
    "  background: rgba(128,128,128,0.10);"
    "}"
    ".ooze-command-sidebar-list row:selected {"
    "  background: @accent_bg_color;"
    "}"
    ".ooze-command-sidebar-list .ooze-command-tab-label {"
    "  color: @sidebar_fg_color;"
    "  font-size: 11px;"
    "}"
    ".ooze-command-sidebar-list row:selected .ooze-command-tab-label {"
    "  color: @accent_fg_color;"
    "}"
    ".ooze-command-tab-actions {"
    "  margin: 4px 6px 8px 6px;"
    "}"
    ".ooze-command-tab-actions .ooze-toolbar-btn {"
    "  margin: 2px 0;"
    "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static void
on_new_tab_clicked (GtkButton *btn G_GNUC_UNUSED, OozeCommandWindow *self)
{
  oc_add_tab (self, TRUE);
}

static void
on_close_tab_clicked (GtkButton *btn G_GNUC_UNUSED, OozeCommandWindow *self)
{
  if (self->active_tab)
    oc_close_tab (self, self->active_tab);
}

static void
ooze_command_window_dispose (GObject *object)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (object);

  while (self->tabs)
    {
      OozeCommandTab *tab = self->tabs->data;
      self->tabs = g_list_delete_link (self->tabs, self->tabs);
      oc_tab_free (tab);
    }
  self->active_tab = NULL;

  G_OBJECT_CLASS (ooze_command_window_parent_class)->dispose (object);
}

static void
ooze_command_window_class_init (OozeCommandWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_command_window_constructed;
  G_OBJECT_CLASS (klass)->dispose = ooze_command_window_dispose;
}

static void
ooze_command_window_init (OozeCommandWindow *self)
{
  self->font_scale = 1.0;
  self->next_tab_id = 1;
}

static void
ooze_command_window_constructed (GObject *object)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (object);
  GtkWidget *paned;
  GtkWidget *scrolled;

  G_OBJECT_CLASS (ooze_command_window_parent_class)->constructed (object);

  oc_ensure_css ();
  ooze_toolbar_ensure_css ();

  gtk_window_set_icon_name (GTK_WINDOW (self), "utilities-terminal");
  gtk_window_set_default_size (GTK_WINDOW (self), 900, 560);
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-command");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Terminal");

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_actions, G_N_ELEMENTS (win_actions),
                                   self);

  paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_wide_handle (GTK_PANED (paned), FALSE);
  gtk_paned_set_resize_start_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_shrink_start_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_resize_end_child (GTK_PANED (paned), TRUE);
  gtk_paned_set_shrink_end_child (GTK_PANED (paned), FALSE);

  /* Spot-like left column: SIDEBAR surface + scrollable tab tiles + New Tab. */
  self->sidebar = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (self->sidebar, 88, -1);
  gtk_widget_add_css_class (self->sidebar, "ooze-command-sidebar");

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  self->tab_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->tab_list),
                                   GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class (self->tab_list, "ooze-command-sidebar-list");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), self->tab_list);
  gtk_box_append (GTK_BOX (self->sidebar), scrolled);

  self->tab_select_handler =
    g_signal_connect (self->tab_list, "row-selected",
                      G_CALLBACK (on_tab_row_selected), self);
  g_signal_connect (self->tab_list, "row-activated",
                    G_CALLBACK (on_tab_row_activated), self);

  {
    GtkWidget *actions;

    actions = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (actions, "ooze-command-tab-actions");
    gtk_widget_set_halign (actions, GTK_ALIGN_CENTER);

    self->new_tab_button = ooze_button_new_toolbar (oc_icon_new_tab,
                                                    "New Tab",
                                                    "Open a new terminal tab");
    gtk_widget_set_halign (self->new_tab_button, GTK_ALIGN_CENTER);
    g_signal_connect (self->new_tab_button, "clicked",
                      G_CALLBACK (on_new_tab_clicked), self);
    gtk_box_append (GTK_BOX (actions), self->new_tab_button);

    self->close_tab_button = ooze_button_new_toolbar (oc_icon_close_tab,
                                                      "Close Tab",
                                                      "Close the selected terminal tab");
    gtk_widget_set_halign (self->close_tab_button, GTK_ALIGN_CENTER);
    g_signal_connect (self->close_tab_button, "clicked",
                      G_CALLBACK (on_close_tab_clicked), self);
    gtk_box_append (GTK_BOX (actions), self->close_tab_button);

    gtk_box_append (GTK_BOX (self->sidebar), actions);
  }

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);

  gtk_paned_set_start_child (GTK_PANED (paned), self->sidebar);
  gtk_paned_set_end_child (GTK_PANED (paned), self->stack);
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), paned);

  oc_add_tab (self, TRUE);
  oc_append_menus (self);
}

GtkWidget *
ooze_command_window_new (GtkApplication *app)
{
  OozeCommandWindow *win;

  win = g_object_new (OOZE_TYPE_COMMAND_WINDOW,
                      "application", app,
                      "standard-edit-actions", FALSE,
                      "standard-menus", FALSE,
                      NULL);

  oc_add_shortcuts (win);
  return GTK_WIDGET (win);
}
