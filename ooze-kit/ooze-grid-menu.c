#include "ooze-grid-menu.h"

#include "ooze-button.h"
#include "ooze-popover.h"

typedef struct
{
  GtkWidget *box;
  GtkWidget *grid;
  guint      column;
  guint      row;
} OozeGridMenuState;

typedef struct
{
  GtkWidget                  *menu;
  OozeGridMenuActivateFunc    activate;
  gpointer                    user_data;
  GDestroyNotify              user_data_destroy;
} OozeGridMenuBinding;

static void
ooze_grid_menu_binding_free (gpointer data,
                             GClosure *closure G_GNUC_UNUSED)
{
  OozeGridMenuBinding *binding = data;

  if (binding->user_data_destroy)
    binding->user_data_destroy (binding->user_data);
  g_free (data);
}

static void
ooze_grid_menu_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay *display;
  GtkCssProvider *provider;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    ".ooze-grid-menu {"
    "  padding: 6px;"
    "}"
    ".ooze-grid-menu-grid {"
    "  column-spacing: 4px;"
    "  row-spacing: 4px;"
    "}"
    ".ooze-grid-menu-tile {"
    "  min-width: 78px;"
    "  min-height: 70px;"
    "  border-radius: 6px;"
    "}"
    ".ooze-grid-menu-tile .ooze-button-label {"
    "  color: @window_fg_color;"
    "}"
    ".ooze-grid-menu-separator {"
    "  min-height: 1px;"
    "  margin: 6px 4px;"
    "  background: @borders;"
    "}");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static void
ooze_grid_menu_item_clicked (GtkButton *button G_GNUC_UNUSED,
                             gpointer   user_data)
{
  OozeGridMenuBinding *binding = user_data;

  gtk_popover_popdown (GTK_POPOVER (binding->menu));
  if (binding->activate)
    binding->activate (binding->user_data);
}

static OozeGridMenuState *
ooze_grid_menu_get_state (GtkWidget *menu)
{
  return g_object_get_data (G_OBJECT (menu), "ooze-grid-menu-state");
}

GtkWidget *
ooze_grid_menu_new (void)
{
  GtkWidget *popover;
  GtkWidget *scroll;
  GtkWidget *box;
  OozeGridMenuState *state;

  ooze_grid_menu_ensure_css ();

  popover = gtk_popover_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
  scroll = gtk_scrolled_window_new ();
  gtk_widget_add_css_class (scroll, "ooze-grid-menu-scroll");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_width (
    GTK_SCROLLED_WINDOW (scroll), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (
    GTK_SCROLLED_WINDOW (scroll), TRUE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (box, "ooze-grid-menu");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), box);
  gtk_popover_set_child (GTK_POPOVER (popover), scroll);

  state = g_new0 (OozeGridMenuState, 1);
  state->box = box;
  state->grid = gtk_grid_new ();
  gtk_widget_add_css_class (state->grid, "ooze-grid-menu-grid");
  gtk_grid_set_column_homogeneous (GTK_GRID (state->grid), TRUE);
  gtk_box_append (GTK_BOX (box), state->grid);
  g_object_set_data_full (G_OBJECT (popover), "ooze-grid-menu-state",
                          state, g_free);

  ooze_popover_fit_screen (GTK_POPOVER (popover));
  return popover;
}

void
ooze_grid_menu_append_item (GtkWidget              *menu,
                            const OozeGridMenuItem *item)
{
  OozeGridMenuState *state;
  GtkWidget *button;
  OozeGridMenuBinding *binding;

  g_return_if_fail (GTK_IS_POPOVER (menu));
  g_return_if_fail (item != NULL);

  state = ooze_grid_menu_get_state (menu);
  g_return_if_fail (state != NULL);

  if (state->column >= 4)
    {
      state->column = 0;
      state->row++;
    }

  button = ooze_button_new_labeled (OOZE_BUTTON_TOOLBAR,
                                    item->icon_names,
                                    32,
                                    item->label,
                                    item->label);
  gtk_widget_add_css_class (button, "ooze-grid-menu-tile");
  gtk_widget_set_sensitive (button, item->sensitive);
  gtk_grid_attach (GTK_GRID (state->grid), button,
                   (int) state->column, (int) state->row, 1, 1);

  binding = g_new (OozeGridMenuBinding, 1);
  binding->menu = menu;
  binding->activate = item->activate;
  binding->user_data = item->user_data;
  binding->user_data_destroy = item->user_data_destroy;
  g_signal_connect_data (button, "clicked",
                         G_CALLBACK (ooze_grid_menu_item_clicked),
                         binding, ooze_grid_menu_binding_free, 0);
  state->column++;
}

void
ooze_grid_menu_append_separator (GtkWidget *menu)
{
  OozeGridMenuState *state;
  GtkWidget *separator;

  g_return_if_fail (GTK_IS_POPOVER (menu));

  state = ooze_grid_menu_get_state (menu);
  g_return_if_fail (state != NULL);

  separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (separator, "ooze-grid-menu-separator");
  gtk_box_append (GTK_BOX (state->box), separator);

  state->grid = gtk_grid_new ();
  gtk_widget_add_css_class (state->grid, "ooze-grid-menu-grid");
  gtk_grid_set_column_homogeneous (GTK_GRID (state->grid), TRUE);
  gtk_box_append (GTK_BOX (state->box), state->grid);
  state->column = 0;
  state->row = 0;
}
