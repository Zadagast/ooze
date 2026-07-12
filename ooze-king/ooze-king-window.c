#include "ooze-king-window.h"
#include "ooze-sound-pane.h"

#include "ooze-button.h"
#include "ooze-header-bar.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <adwaita.h>

typedef struct {
  char *pane;
  char *title;
} KingNavEntry;

struct _OozeKingWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *header;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *tile_sound;
  GtkWidget *tile_appearance;
  GtkWidget *tile_about;
  GtkWidget *grid;
  GtkWidget *stack;
  GtkWidget *detail;

  char  *current_pane;
  char  *current_title;
  GList *back_stack;
  GList *forward_stack;
};

G_DEFINE_FINAL_TYPE (OozeKingWindow, ooze_king_window, GTK_TYPE_APPLICATION_WINDOW)

static const char * const king_icon_back[] = {
  "go-previous", "go-previous-symbolic", NULL
};
static const char * const king_icon_forward[] = {
  "go-next", "go-next-symbolic", NULL
};
static const char * const king_icon_sound[] = {
  "preferences-desktop-sound", "audio-volume-high", NULL
};
static const char * const king_icon_appearance[] = {
  "preferences-desktop-theme", "preferences-desktop", NULL
};
static const char * const king_icon_about[] = {
  "system-help", "help-about", "dialog-information", NULL
};

static void
king_nav_entry_free (gpointer p)
{
  KingNavEntry *e = p;
  if (!e)
    return;
  g_free (e->pane);
  g_free (e->title);
  g_free (e);
}

static KingNavEntry *
king_nav_entry_new (const char *pane, const char *title)
{
  KingNavEntry *e = g_new0 (KingNavEntry, 1);
  e->pane = g_strdup (pane);
  e->title = g_strdup (title);
  return e;
}

static void
king_update_nav_buttons (OozeKingWindow *self)
{
  gtk_widget_set_sensitive (self->back_button, self->back_stack != NULL);
  gtk_widget_set_sensitive (self->forward_button, self->forward_stack != NULL);
}

static void
king_update_grid_for_pane (OozeKingWindow *self, const char *pane)
{
  gboolean home = (g_strcmp0 (pane, "home") == 0);

  gtk_widget_set_visible (self->tile_sound, home);
  gtk_widget_set_visible (self->tile_appearance, home);
  gtk_widget_set_visible (self->tile_about, home);
  gtk_widget_set_visible (self->detail, !home);
}

static void
king_apply_location (OozeKingWindow *self, const char *pane, const char *title)
{
  g_free (self->current_pane);
  g_free (self->current_title);
  self->current_pane = g_strdup (pane);
  self->current_title = g_strdup (title);

  if (g_strcmp0 (pane, "home") == 0)
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
  else
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), pane);

  king_update_grid_for_pane (self, pane);
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), title);
  gtk_window_set_title (GTK_WINDOW (self), title);
  king_update_nav_buttons (self);
}

static void
king_navigate_to (OozeKingWindow *self,
                  const char     *pane,
                  const char     *title,
                  gboolean        push_history)
{
  if (!pane || !title)
    return;

  if (self->current_pane && g_strcmp0 (self->current_pane, pane) == 0)
    return;

  if (push_history && self->current_pane)
    {
      self->back_stack = g_list_prepend (self->back_stack,
                                         king_nav_entry_new (self->current_pane,
                                                             self->current_title));
      g_list_free_full (self->forward_stack, king_nav_entry_free);
      self->forward_stack = NULL;
    }

  king_apply_location (self, pane, title);
}

static void
king_pop_history (OozeKingWindow *self,
                  GList         **stack,
                  GList         **other_stack)
{
  GList *link;
  KingNavEntry *entry;

  if (!*stack)
    return;

  link = *stack;
  entry = link->data;
  *stack = g_list_remove_link (*stack, link);
  g_list_free_1 (link);

  if (self->current_pane)
    *other_stack = g_list_prepend (*other_stack,
                                   king_nav_entry_new (self->current_pane,
                                                       self->current_title));

  king_apply_location (self, entry->pane, entry->title);
  king_nav_entry_free (entry);
}

static void
on_back_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKingWindow *self)
{
  king_pop_history (self, &self->back_stack, &self->forward_stack);
}

static void
on_forward_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKingWindow *self)
{
  king_pop_history (self, &self->forward_stack, &self->back_stack);
}

static void
on_tile_clicked (GtkButton *btn, OozeKingWindow *self)
{
  const char *pane = g_object_get_data (G_OBJECT (btn), "pane");
  const char *title = g_object_get_data (G_OBJECT (btn), "title");

  if (pane && title)
    king_navigate_to (self, pane, title, TRUE);
}

static void
on_dark_toggled (GtkSwitch *sw, GParamSpec *pspec G_GNUC_UNUSED,
                 gpointer user_data G_GNUC_UNUSED)
{
  adw_style_manager_set_color_scheme (
      adw_style_manager_get_default (),
      gtk_switch_get_active (sw) ? ADW_COLOR_SCHEME_FORCE_DARK
                                 : ADW_COLOR_SCHEME_FORCE_LIGHT);
}

static GtkWidget *
make_nav_tile (OozeKingWindow         *self,
               const char * const     *icons,
               const char             *label,
               const char             *tooltip,
               GCallback               clicked)
{
  GtkWidget *btn;

  btn = ooze_button_new_toolbar (icons, label, tooltip);
  gtk_widget_add_css_class (btn, "ooze-settings-tile");
  g_signal_connect (btn, "clicked", clicked, self);
  return btn;
}

static GtkWidget *
make_settings_tile (OozeKingWindow         *self,
                    const char * const     *icons,
                    const char             *title,
                    const char             *subtitle,
                    const char             *pane)
{
  GtkWidget *btn;
  GtkWidget *box;
  GtkWidget *sub;

  btn = ooze_button_new_toolbar (icons, title, title);
  gtk_widget_add_css_class (btn, "ooze-settings-tile");
  g_object_set_data (G_OBJECT (btn), "pane", (gpointer) pane);
  g_object_set_data (G_OBJECT (btn), "title", (gpointer) title);
  g_signal_connect (btn, "clicked", G_CALLBACK (on_tile_clicked), self);

  box = gtk_button_get_child (GTK_BUTTON (btn));
  if (GTK_IS_BOX (box) && subtitle)
    {
      sub = gtk_label_new (subtitle);
      gtk_label_set_xalign (GTK_LABEL (sub), 0.5);
      gtk_label_set_wrap (GTK_LABEL (sub), TRUE);
      gtk_label_set_justify (GTK_LABEL (sub), GTK_JUSTIFY_CENTER);
      gtk_widget_add_css_class (sub, "ooze-settings-tile-sub");
      gtk_box_append (GTK_BOX (box), sub);
    }

  return btn;
}

static GtkWidget *
make_appearance_pane (void)
{
  GtkWidget *box;
  GtkWidget *card;
  GtkWidget *row;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *toggle;
  AdwStyleManager *sm;
  AdwColorScheme scheme;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (box, 16);
  gtk_widget_set_margin_bottom (box, 24);
  gtk_widget_set_margin_start (box, 24);
  gtk_widget_set_margin_end (box, 24);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);

  card = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (card), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (card, "boxed-list");
  gtk_widget_set_size_request (card, 380, -1);

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (hbox, 12);
  gtk_widget_set_margin_bottom (hbox, 12);
  gtk_widget_set_margin_start (hbox, 14);
  gtk_widget_set_margin_end (hbox, 14);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), hbox);

  label = gtk_label_new ("Dark Mode");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (hbox), label);

  sm = adw_style_manager_get_default ();
  scheme = adw_style_manager_get_color_scheme (sm);
  toggle = gtk_switch_new ();
  gtk_switch_set_active (GTK_SWITCH (toggle),
                         scheme == ADW_COLOR_SCHEME_FORCE_DARK
                         || (scheme == ADW_COLOR_SCHEME_DEFAULT
                             && adw_style_manager_get_dark (sm)));
  g_signal_connect (toggle, "notify::active", G_CALLBACK (on_dark_toggled), NULL);
  gtk_box_append (GTK_BOX (hbox), toggle);

  gtk_list_box_append (GTK_LIST_BOX (card), row);
  gtk_box_append (GTK_BOX (box), card);
  return box;
}

static GtkWidget *
make_about_pane (void)
{
  GtkWidget *box;
  GtkWidget *title;
  GtkWidget *body;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (box, 32);
  gtk_widget_set_margin_bottom (box, 32);
  gtk_widget_set_margin_start (box, 32);
  gtk_widget_set_margin_end (box, 32);

  title = gtk_label_new ("Ooze");
  gtk_widget_add_css_class (title, "title-1");
  gtk_box_append (GTK_BOX (box), title);

  body = gtk_label_new ("A Wayland desktop with Aqua-inspired chrome.\n"
                        "System Settings configures this desktop.");
  gtk_label_set_justify (GTK_LABEL (body), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (body), TRUE);
  gtk_widget_add_css_class (body, "dim-label");
  gtk_box_append (GTK_BOX (box), body);

  return box;
}

static void
ooze_king_window_dispose (GObject *object)
{
  OozeKingWindow *self = OOZE_KING_WINDOW (object);

  g_clear_pointer (&self->current_pane, g_free);
  g_clear_pointer (&self->current_title, g_free);
  g_list_free_full (self->back_stack, king_nav_entry_free);
  g_list_free_full (self->forward_stack, king_nav_entry_free);
  self->back_stack = NULL;
  self->forward_stack = NULL;

  G_OBJECT_CLASS (ooze_king_window_parent_class)->dispose (object);
}

static void
ooze_king_window_class_init (OozeKingWindowClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_king_window_dispose;
}

static void
ooze_king_window_init (OozeKingWindow *self)
{
  GtkWidget *shell;
  GtkWidget *surface;
  GtkWidget *empty;
  GtkWidget *sound;
  GtkWidget *appearance;
  GtkWidget *about;

  ooze_toolbar_ensure_css ();

  gtk_window_set_default_size (GTK_WINDOW (self), 640, 520);
  gtk_window_set_title (GTK_WINDOW (self), "System Settings");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-king");
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "System Settings");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_box_append (GTK_BOX (shell), surface);

  /* Back / Forward live in the same settings grid as Sound / Appearance / About. */
  self->grid = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->grid), 5);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->grid), 3);
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid), TRUE);
  gtk_widget_add_css_class (self->grid, "ooze-settings-grid");
  gtk_widget_set_halign (self->grid, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (surface), self->grid);

  self->back_button = make_nav_tile (self, king_icon_back, "Back", "Back",
                                     G_CALLBACK (on_back_clicked));
  self->forward_button = make_nav_tile (self, king_icon_forward, "Forward", "Forward",
                                        G_CALLBACK (on_forward_clicked));
  self->tile_sound = make_settings_tile (self, king_icon_sound,
                                         "Sound", "Output, input & apps", "sound");
  self->tile_appearance = make_settings_tile (self, king_icon_appearance,
                                              "Appearance", "Light and dark", "appearance");
  self->tile_about = make_settings_tile (self, king_icon_about,
                                         "About", "Ooze desktop", "about");

  gtk_flow_box_append (GTK_FLOW_BOX (self->grid), self->back_button);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid), self->forward_button);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid), self->tile_sound);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid), self->tile_appearance);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid), self->tile_about);

  self->detail = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand (self->detail, TRUE);
  gtk_widget_set_vexpand (self->detail, TRUE);
  gtk_box_append (GTK_BOX (surface), self->detail);

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);
  gtk_box_append (GTK_BOX (self->detail), self->stack);

  empty = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  sound = ooze_sound_pane_new ();
  appearance = make_appearance_pane ();
  about = make_about_pane ();

  gtk_stack_add_named (GTK_STACK (self->stack), empty, "empty");
  gtk_stack_add_named (GTK_STACK (self->stack), sound, "sound");
  gtk_stack_add_named (GTK_STACK (self->stack), appearance, "appearance");
  gtk_stack_add_named (GTK_STACK (self->stack), about, "about");

  gtk_window_set_child (GTK_WINDOW (self), shell);
  king_apply_location (self, "home", "System Settings");

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
ooze_king_window_new (GtkApplication *app)
{
  return g_object_new (OOZE_KING_TYPE_WINDOW, "application", app, NULL);
}
