#include "ooze-command-window.h"

#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-icons.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"
#include "ooze-about.h"

#include <adwaita.h>
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
  GtkWidget         *chip;       /* tab button in the strip */
  GtkWidget         *title_label;
  guint              id;
  guint              close_idle_id;
};

struct _OozeCommandWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *header;
  GtkWidget *tab_bar;
  GtkWidget *tab_strip;
  GtkWidget *new_tab_button;
  GtkWidget *stack;

  GList            *tabs;
  OozeCommandTab   *active_tab;
  guint             next_tab_id;
  double            font_scale;
};

G_DEFINE_FINAL_TYPE (OozeCommandWindow, ooze_command_window,
                     GTK_TYPE_APPLICATION_WINDOW)

static const char * const oc_icon_new_tab[] = {
  "list-add", "tab-new-symbolic", "list-add-symbolic", NULL
};

static void oc_select_tab (OozeCommandWindow *self, OozeCommandTab *tab);
static void oc_close_tab  (OozeCommandWindow *self, OozeCommandTab *tab);
static OozeCommandTab *oc_add_tab (OozeCommandWindow *self, gboolean select);
static void oc_apply_font_to_terminal (OozeCommandWindow *self, VteTerminal *vte);
static void oc_update_window_title (OozeCommandWindow *self);

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

  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), title);
  gtk_window_set_title (GTK_WINDOW (self), title);
}

static void
oc_refresh_tab_chips (OozeCommandWindow *self)
{
  gsize n;
  gsize i;
  gsize active = G_MAXSIZE;
  GList *l;
  g_autofree GtkWidget **peers = NULL;

  n = g_list_length (self->tabs);
  if (n == 0)
    return;

  peers = g_new0 (GtkWidget *, n);
  for (l = self->tabs, i = 0; l != NULL; l = l->next, i++)
    {
      OozeCommandTab *tab = l->data;
      peers[i] = tab->chip;
      if (tab == self->active_tab)
        active = i;
    }

  ooze_button_set_exclusive (peers, n, active);
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
  envp = g_get_environ ();

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
on_tab_close_pressed (GtkGestureClick *gesture,
                      int              n_press G_GNUC_UNUSED,
                      double           x G_GNUC_UNUSED,
                      double           y G_GNUC_UNUSED,
                      OozeCommandTab  *tab)
{
  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  if (tab && tab->window)
    oc_close_tab (tab->window, tab);
}

static void
on_tab_chip_clicked (GtkButton *btn, gpointer user_data G_GNUC_UNUSED)
{
  OozeCommandTab *tab = g_object_get_data (G_OBJECT (btn), "tab");
  if (tab && tab->window)
    oc_select_tab (tab->window, tab);
}

static GtkWidget *
oc_make_tab_chip (OozeCommandTab *tab)
{
  GtkWidget *chip;
  GtkWidget *box;
  GtkWidget *close_img;
  GtkGesture *close_click;

  /* Real OozeButton so the active glass plate is painted by the kit. */
  chip = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  gtk_widget_add_css_class (chip, "ooze-command-tab");
  g_object_set_data (G_OBJECT (chip), "tab", tab);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign (box, GTK_ALIGN_FILL);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

  tab->title_label = gtk_label_new ("Terminal");
  gtk_label_set_ellipsize (GTK_LABEL (tab->title_label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (tab->title_label), 18);
  gtk_widget_add_css_class (tab->title_label, "ooze-button-label");
  gtk_widget_set_hexpand (tab->title_label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (tab->title_label), 0.0);
  gtk_box_append (GTK_BOX (box), tab->title_label);

  /* Image + gesture — not a nested GtkButton (illegal inside GtkButton). */
  close_img = gtk_image_new_from_icon_name ("window-close-symbolic");
  gtk_image_set_pixel_size (GTK_IMAGE (close_img), 12);
  gtk_widget_add_css_class (close_img, "ooze-command-tab-close");
  gtk_widget_set_valign (close_img, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target (close_img, TRUE);
  gtk_widget_set_cursor_from_name (close_img, "pointer");
  close_click = gtk_gesture_click_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (close_click),
                                              GTK_PHASE_CAPTURE);
  g_signal_connect (close_click, "pressed", G_CALLBACK (on_tab_close_pressed), tab);
  gtk_widget_add_controller (close_img, GTK_EVENT_CONTROLLER (close_click));
  gtk_box_append (GTK_BOX (box), close_img);

  gtk_button_set_child (GTK_BUTTON (chip), box);
  g_signal_connect (chip, "clicked", G_CALLBACK (on_tab_chip_clicked), NULL);

  return chip;
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
  gtk_box_append (GTK_BOX (self->tab_strip), tab->chip);

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

  if (!self || !tab)
    return;

  link = g_list_find (self->tabs, tab);
  if (!link)
    return;

  if (link->next)
    next = link->next->data;
  else if (link->prev)
    next = link->prev->data;

  self->tabs = g_list_delete_link (self->tabs, link);

  if (self->active_tab == tab)
    self->active_tab = NULL;

  if (tab->chip && self->tab_strip)
    gtk_box_remove (GTK_BOX (self->tab_strip), tab->chip);
  if (tab->page && self->stack)
    gtk_stack_remove (GTK_STACK (self->stack), tab->page);

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
    gtk_window_destroy (GTK_WINDOW (self));
}

static void
action_about (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Command",
                      "utilities-terminal",
                      "Terminal emulator for Ooze Desktop.");
}

static const GActionEntry win_actions[] = {
  { "copy",        action_copy,       NULL, NULL, NULL },
  { "paste",       action_paste,      NULL, NULL, NULL },
  { "select-all",  action_select_all, NULL, NULL, NULL },
  { "clear",       action_clear,      NULL, NULL, NULL },
  { "zoom-in",     action_zoom_in,    NULL, NULL, NULL },
  { "zoom-out",    action_zoom_out,   NULL, NULL, NULL },
  { "zoom-reset",  action_zoom_reset, NULL, NULL, NULL },
  { "new-window",  action_new_window, NULL, NULL, NULL },
  { "new-tab",     action_new_tab,    NULL, NULL, NULL },
  { "close",       action_close,      NULL, NULL, NULL },
  { "about",       action_about,      NULL, NULL, NULL },
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

static GMenuModel *
oc_build_menubar (void)
{
  GMenu *bar, *file, *edit, *view, *window, *help;
  GMenuItem *item;

  bar = g_menu_new ();

  file = g_menu_new ();
  g_menu_append (file, "New Tab",     "win.new-tab");
  g_menu_append (file, "New Window",  "win.new-window");
  g_menu_append (file, "Close Tab",   "win.close");
  item = g_menu_item_new_submenu ("File", G_MENU_MODEL (file));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (file);

  edit = g_menu_new ();
  g_menu_append (edit, "Copy",        "win.copy");
  g_menu_append (edit, "Paste",       "win.paste");
  g_menu_append (edit, "Select All",  "win.select-all");
  g_menu_append (edit, "Clear Scrollback", "win.clear");
  item = g_menu_item_new_submenu ("Edit", G_MENU_MODEL (edit));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (edit);

  view = g_menu_new ();
  g_menu_append (view, "Zoom In",     "win.zoom-in");
  g_menu_append (view, "Zoom Out",    "win.zoom-out");
  g_menu_append (view, "Normal Size", "win.zoom-reset");
  item = g_menu_item_new_submenu ("View", G_MENU_MODEL (view));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (view);

  window = g_menu_new ();
  g_menu_append (window, "Minimize",  "win.minimize");
  g_menu_append (window, "Maximize",  "win.maximize");
  item = g_menu_item_new_submenu ("Window", G_MENU_MODEL (window));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (window);

  help = g_menu_new ();
  g_menu_append (help, "About Ooze Command", "win.about");
  item = g_menu_item_new_submenu ("Help", G_MENU_MODEL (help));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (help);

  return G_MENU_MODEL (bar);
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
    ".ooze-command-tab-bar {"
    "  padding: 4px 8px;"
    "  min-height: 52px;"
    "}"
    ".ooze-command-tab-strip {"
    "  /* spacing set via gtk_box_set_spacing */"
    "}"
    ".ooze-command-tab {"
    "  min-width: 96px;"
    "  margin: 0 1px;"
    "}"
    ".ooze-command-tab-close {"
    "  min-width: 18px;"
    "  min-height: 18px;"
    "  opacity: 0.65;"
    "}"
    ".ooze-command-tab-close:hover { opacity: 1.0; }"
    ".ooze-command-new-tab {"
    "  min-width: 72px;"
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
  G_OBJECT_CLASS (klass)->dispose = ooze_command_window_dispose;
}

static void
ooze_command_window_init (OozeCommandWindow *self)
{
  GtkWidget *shell;
  GtkWidget *spacer;

  self->font_scale = 1.0;
  self->next_tab_id = 1;

  oc_ensure_css ();
  ooze_toolbar_ensure_css ();

  gtk_window_set_title (GTK_WINDOW (self), "Terminal");
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Command");
  gtk_window_set_default_size (GTK_WINDOW (self), 800, 520);
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-command");

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_actions, G_N_ELEMENTS (win_actions),
                                   self);

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header),
                                 GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "Terminal");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->tab_bar = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (self->tab_bar), 6);
  gtk_widget_add_css_class (self->tab_bar, "ooze-command-tab-bar");
  gtk_widget_add_css_class (self->tab_bar, "ooze-toolbar");
  gtk_box_append (GTK_BOX (shell), self->tab_bar);

  self->tab_strip = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class (self->tab_strip, "ooze-command-tab-strip");
  gtk_widget_set_hexpand (self->tab_strip, TRUE);
  gtk_widget_set_halign (self->tab_strip, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (self->tab_bar), self->tab_strip);

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (self->tab_bar), spacer);

  self->new_tab_button = ooze_button_new_toolbar (oc_icon_new_tab,
                                                  "New Tab",
                                                  "Open a new terminal tab");
  gtk_widget_add_css_class (self->new_tab_button, "ooze-command-new-tab");
  g_signal_connect (self->new_tab_button, "clicked",
                    G_CALLBACK (on_new_tab_clicked), self);
  gtk_box_append (GTK_BOX (self->tab_bar), self->new_tab_button);

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_box_append (GTK_BOX (shell), self->stack);

  gtk_window_set_child (GTK_WINDOW (self), shell);

  g_signal_connect_swapped (adw_style_manager_get_default (), "notify::dark",
                            G_CALLBACK (gtk_widget_queue_draw), self);

  oc_add_tab (self, TRUE);
}

GtkWidget *
ooze_command_window_new (GtkApplication *app)
{
  OozeCommandWindow *win;
  GMenuModel *menubar;

  win = g_object_new (OOZE_TYPE_COMMAND_WINDOW,
                      "application", app,
                      NULL);

  /* Application is only available after construction — set menubar here. */
  menubar = oc_build_menubar ();
  gtk_application_set_menubar (app, menubar);
  g_object_unref (menubar);
  gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (win), FALSE);

  oc_add_shortcuts (win);
  return GTK_WIDGET (win);
}
