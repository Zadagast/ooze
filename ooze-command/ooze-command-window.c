#include "ooze-command-window.h"

#include "ooze-header-bar.h"

#include <adwaita.h>
#include <vte/vte.h>
#include <pango/pango.h>

/* ── Dark terminal colour palette (Ooze identity) ───────────────────────── */
/* Background matches the OozeHeaderBar charcoal so the two surfaces read as
 * one unified dark chrome, with only the scrollback region in a lighter ink. */
#define OC_BG_R   0.110   /* #1c1c1e */
#define OC_BG_G   0.110
#define OC_BG_B   0.118

#define OC_FG_R   0.878   /* #e0e0e0 */
#define OC_FG_G   0.878
#define OC_FG_B   0.878

/* 16-colour ANSI palette – kept readable on the dark background */
static const GdkRGBA oc_palette[16] = {
  { 0.137, 0.137, 0.145, 1.0 }, /* 0  black      #232325 */
  { 0.902, 0.235, 0.235, 1.0 }, /* 1  red         #e63c3c */
  { 0.188, 0.749, 0.376, 1.0 }, /* 2  green       #30bf60 */
  { 0.902, 0.718, 0.235, 1.0 }, /* 3  yellow      #e6b73c */
  { 0.196, 0.467, 0.843, 1.0 }, /* 4  blue        #3277d7 – Ooze blue */
  { 0.686, 0.259, 0.863, 1.0 }, /* 5  magenta     #af42dc */
  { 0.188, 0.749, 0.749, 1.0 }, /* 6  cyan        #30bfbf */
  { 0.784, 0.784, 0.800, 1.0 }, /* 7  white       #c8c8cc */
  { 0.376, 0.376, 0.408, 1.0 }, /* 8  brblack     #606068 */
  { 1.000, 0.427, 0.427, 1.0 }, /* 9  brred        #ff6d6d */
  { 0.349, 0.898, 0.541, 1.0 }, /* 10 brgreen      #59e58a */
  { 1.000, 0.855, 0.431, 1.0 }, /* 11 bryellow     #ffdA6e */
  { 0.329, 0.588, 1.000, 1.0 }, /* 12 brblue       #5496ff */
  { 0.847, 0.435, 1.000, 1.0 }, /* 13 brmagenta    #d86fff */
  { 0.349, 0.898, 0.898, 1.0 }, /* 14 brcyan       #59e5e5 */
  { 0.941, 0.941, 0.941, 1.0 }, /* 15 brwhite      #f0f0f0 */
};

struct _OozeCommandWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *header;
  GtkWidget *terminal;
  GPid       child_pid;

  double     font_scale; /* 1.0 = 100 % */
};

G_DEFINE_FINAL_TYPE (OozeCommandWindow, ooze_command_window,
                     GTK_TYPE_APPLICATION_WINDOW)

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void
oc_apply_colors (VteTerminal *vte)
{
  const GdkRGBA bg     = { OC_BG_R, OC_BG_G, OC_BG_B, 1.0 };
  const GdkRGBA fg     = { OC_FG_R, OC_FG_G, OC_FG_B, 1.0 };
  const GdkRGBA cursor = { 0.196, 0.467, 0.843, 1.0 };  /* Ooze blue */

  vte_terminal_set_colors (vte, &fg, &bg, oc_palette, G_N_ELEMENTS (oc_palette));
  vte_terminal_set_color_cursor (vte, &cursor);
  vte_terminal_set_color_highlight (vte, &(GdkRGBA){ 0.196, 0.467, 0.843, 0.35 });
}

static void
oc_apply_font (OozeCommandWindow *self)
{
  PangoFontDescription *fd =
    pango_font_description_from_string ("Monospace 11");
  vte_terminal_set_font (VTE_TERMINAL (self->terminal), fd);
  vte_terminal_set_font_scale (VTE_TERMINAL (self->terminal), self->font_scale);
  pango_font_description_free (fd);
}

/* ── Signal handlers ─────────────────────────────────────────────────────── */

static void
on_title_changed (VteTerminal       *vte,
                  OozeCommandWindow *self)
{
  const char *title = vte_terminal_get_window_title (vte);

  if (title && *title)
    {
      ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), title);
      gtk_window_set_title (GTK_WINDOW (self), title);
    }
  else
    {
      ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header),
                                 "Ooze Command");
      gtk_window_set_title (GTK_WINDOW (self), "Ooze Command");
    }
}

static void
on_child_exited (VteTerminal       *vte G_GNUC_UNUSED,
                 int                status G_GNUC_UNUSED,
                 OozeCommandWindow *self)
{
  gtk_window_destroy (GTK_WINDOW (self));
}

static void
oc_spawn_shell (OozeCommandWindow *self)
{
  const char  *shell;
  const char  *argv[2];
  char       **envp;

  shell   = vte_get_user_shell ();
  if (!shell || !*shell)
    shell = "/bin/bash";

  argv[0] = shell;
  argv[1] = NULL;
  envp    = g_get_environ ();

  vte_terminal_spawn_async (
    VTE_TERMINAL (self->terminal),
    VTE_PTY_DEFAULT,
    NULL,                 /* working directory – inherit */
    (char **) argv,
    envp,
    G_SPAWN_SEARCH_PATH,
    NULL, NULL, NULL,     /* child-setup, user-data, destroy-notify */
    -1,                   /* timeout */
    NULL,                 /* cancellable */
    NULL, NULL            /* callback, user-data */
  );

  g_strfreev (envp);
}

/* ── Actions ─────────────────────────────────────────────────────────────── */

static void
action_copy (GSimpleAction *a G_GNUC_UNUSED,
             GVariant      *p G_GNUC_UNUSED,
             gpointer       ud)
{
  VteTerminal *vte = VTE_TERMINAL (OOZE_COMMAND_WINDOW (ud)->terminal);
  if (vte_terminal_get_has_selection (vte))
    vte_terminal_copy_clipboard_format (vte, VTE_FORMAT_TEXT);
}

static void
action_paste (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  vte_terminal_paste_clipboard (VTE_TERMINAL (OOZE_COMMAND_WINDOW (ud)->terminal));
}

static void
action_select_all (GSimpleAction *a G_GNUC_UNUSED,
                   GVariant      *p G_GNUC_UNUSED,
                   gpointer       ud)
{
  vte_terminal_select_all (VTE_TERMINAL (OOZE_COMMAND_WINDOW (ud)->terminal));
}

static void
action_clear (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  vte_terminal_set_scrollback_lines (
    VTE_TERMINAL (OOZE_COMMAND_WINDOW (ud)->terminal), 0);
  vte_terminal_set_scrollback_lines (
    VTE_TERMINAL (OOZE_COMMAND_WINDOW (ud)->terminal), 10000);
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
  GtkApplication    *app  = gtk_window_get_application (GTK_WINDOW (ud));
  GtkWidget         *win  = ooze_command_window_new (app);

  gtk_application_add_window (app, GTK_WINDOW (win));
  gtk_window_present (GTK_WINDOW (win));
}

static void
action_close (GSimpleAction *a G_GNUC_UNUSED,
              GVariant      *p G_GNUC_UNUSED,
              gpointer       ud)
{
  gtk_window_destroy (GTK_WINDOW (ud));
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
  { "close",       action_close,      NULL, NULL, NULL },
};

/* ── Keyboard shortcuts ──────────────────────────────────────────────────── */

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
    { "win.close",      "<Control>q"        },
  };
  GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));

  for (gsize i = 0; i < G_N_ELEMENTS (map); i++)
    {
      const char *accels[] = { map[i].accel, NULL };
      gtk_application_set_accels_for_action (app, map[i].action, accels);
    }
}

/* ── Menubar (exported as GMenuModel for the Ooze global menu) ──────────── */

static GMenuModel *
oc_build_menubar (void)
{
  GMenu *bar, *file, *edit, *view, *window;
  GMenuItem *item;

  bar = g_menu_new ();

  /* File */
  file = g_menu_new ();
  g_menu_append (file, "New Window",  "win.new-window");
  g_menu_append (file, "Close",       "win.close");
  item = g_menu_item_new_submenu ("File", G_MENU_MODEL (file));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (file);

  /* Edit */
  edit = g_menu_new ();
  g_menu_append (edit, "Copy",        "win.copy");
  g_menu_append (edit, "Paste",       "win.paste");
  g_menu_append (edit, "Select All",  "win.select-all");
  g_menu_append (edit, "Clear Scrollback", "win.clear");
  item = g_menu_item_new_submenu ("Edit", G_MENU_MODEL (edit));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (edit);

  /* View */
  view = g_menu_new ();
  g_menu_append (view, "Zoom In",     "win.zoom-in");
  g_menu_append (view, "Zoom Out",    "win.zoom-out");
  g_menu_append (view, "Normal Size", "win.zoom-reset");
  item = g_menu_item_new_submenu ("View", G_MENU_MODEL (view));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (view);

  /* Window */
  window = g_menu_new ();
  g_menu_append (window, "Minimize",  "win.minimize");
  g_menu_append (window, "Maximize",  "win.maximize");
  item = g_menu_item_new_submenu ("Window", G_MENU_MODEL (window));
  g_menu_append_item (bar, item);
  g_object_unref (item);
  g_object_unref (window);

  return G_MENU_MODEL (bar);
}

/* ── CSS ─────────────────────────────────────────────────────────────────── */

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
    /* CSD shadow + corner radius — same Aqua-shadow profile as Spot */
    "window.csd.ooze-command > decoration {"
    "  border-radius: 9px;"
    "  box-shadow:"
    "    0 2px  6px rgba(0,0,0,0.28),"
    "    0 8px 24px rgba(0,0,0,0.50),"
    "    0 20px 44px rgba(0,0,0,0.28);"
    "}"
    /* The terminal itself — let VTE own all colours inside.
     * We only constrain sizing so the window isn't born infinitely small. */
    "vte-terminal {"
    "  min-width:  480px;"
    "  min-height: 280px;"
    "}"
  );
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

/* ── GtkWidget / GObject boilerplate ─────────────────────────────────────── */

static void
ooze_command_window_dispose (GObject *object)
{
  OozeCommandWindow *self = OOZE_COMMAND_WINDOW (object);

  if (self->child_pid > 0)
    {
      g_spawn_close_pid (self->child_pid);
      self->child_pid = 0;
    }

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
  GMenuModel *menubar;
  GtkWidget  *scrolled;

  self->font_scale = 1.0;

  oc_ensure_css ();

  gtk_window_set_title (GTK_WINDOW (self), "Ooze Command");
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Command");
  gtk_window_set_default_size (GTK_WINDOW (self), 800, 520);
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-command");

  /* Register window actions */
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_actions, G_N_ELEMENTS (win_actions),
                                   self);

  /* ── Header bar (OozeHeaderBar = dark charcoal + traffic lights) ── */
  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header),
                                 GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "Ooze Command");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  /* ── VTE terminal ───────────────────────────────────────────────── */
  self->terminal = vte_terminal_new ();

  vte_terminal_set_audible_bell         (VTE_TERMINAL (self->terminal), FALSE);
  vte_terminal_set_scroll_on_keystroke  (VTE_TERMINAL (self->terminal), TRUE);
  vte_terminal_set_scroll_on_output     (VTE_TERMINAL (self->terminal), FALSE);
  vte_terminal_set_scrollback_lines     (VTE_TERMINAL (self->terminal), 10000);
  vte_terminal_set_cursor_blink_mode    (VTE_TERMINAL (self->terminal),
                                         VTE_CURSOR_BLINK_OFF);
  vte_terminal_set_cursor_shape         (VTE_TERMINAL (self->terminal),
                                         VTE_CURSOR_SHAPE_BLOCK);
  vte_terminal_set_bold_is_bright       (VTE_TERMINAL (self->terminal), TRUE);
  vte_terminal_set_allow_hyperlink      (VTE_TERMINAL (self->terminal), TRUE);

  oc_apply_colors (VTE_TERMINAL (self->terminal));
  oc_apply_font (self);

  {
    GMenu *ctx = g_menu_new ();

    g_menu_append (ctx, "Copy", "win.copy");
    g_menu_append (ctx, "Paste", "win.paste");
    g_menu_append (ctx, "Select All", "win.select-all");
    vte_terminal_set_context_menu_model (VTE_TERMINAL (self->terminal),
                                         G_MENU_MODEL (ctx));
    g_object_unref (ctx);
  }

  /* Redraw header when dark/light mode changes (OozeHeaderBar is dark-aware) */
  g_signal_connect_swapped (adw_style_manager_get_default (), "notify::dark",
                             G_CALLBACK (gtk_widget_queue_draw), self);

  g_signal_connect (self->terminal, "notify::window-title",
                    G_CALLBACK (on_title_changed), self);
  g_signal_connect (self->terminal, "child-exited",
                    G_CALLBACK (on_child_exited), self);

  /* Wrap in a scrolled window so the terminal can scroll its own history */
  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->terminal, TRUE);
  gtk_widget_set_vexpand (self->terminal, TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled),
                                 self->terminal);

  gtk_window_set_child (GTK_WINDOW (self), scrolled);

  /* ── Global menu ─────────────────────────────────────────────────── */
  menubar = oc_build_menubar ();
  gtk_application_set_menubar (gtk_window_get_application (GTK_WINDOW (self)),
                                menubar);
  g_object_unref (menubar);

  /* Spawn the user's shell once the widget tree is realised */
  oc_spawn_shell (self);
}

/* ── Public constructor ──────────────────────────────────────────────────── */

GtkWidget *
ooze_command_window_new (GtkApplication *app)
{
  OozeCommandWindow *win;

  win = g_object_new (OOZE_TYPE_COMMAND_WINDOW,
                      "application", app,
                      NULL);
  oc_add_shortcuts (win);
  return GTK_WIDGET (win);
}
