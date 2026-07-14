#include "ooze-themes-pane.h"

#include "ooze-foreign-gtk.h"
#include "ooze-icon-lookup.h"
#include "ooze-scroll.h"
#include "ooze-shared-icons.h"
#include "ooze-surface.h"

struct _OozeThemesPane
{
  GtkBox parent_instance;
  GSettings *interface_settings;
  GtkWidget *appearance;
  GtkWidget *icons;
  GtkWidget *cursors;
  GtkWidget *cursor_size;
  GtkWidget *foreign_gtk;
  gulong color_scheme_handler;
  gboolean filling;
};

G_DEFINE_FINAL_TYPE (OozeThemesPane, ooze_themes_pane, GTK_TYPE_BOX)

static GtkWidget *
make_row (const char *title,
          GtkWidget  *control)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *label = gtk_label_new (title);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (row), label);
  gtk_box_append (GTK_BOX (row), control);
  return row;
}

static GtkStringList *
string_list_from_strv (char       **items,
                       const char  *first)
{
  g_autoptr (GPtrArray) values = g_ptr_array_new ();
  guint i;

  if (first && first[0] != '\0')
    g_ptr_array_add (values, (gpointer) first);

  for (i = 0; items && items[i]; i++)
    {
      if (first && g_strcmp0 (items[i], first) == 0)
        continue;
      g_ptr_array_add (values, items[i]);
    }

  g_ptr_array_add (values, NULL);
  return gtk_string_list_new ((const char * const *) values->pdata);
}

static guint
find_string (GtkDropDown *drop_down,
             const char  *value)
{
  GListModel *model;
  guint n;
  guint i;

  if (!value || !GTK_IS_DROP_DOWN (drop_down))
    return GTK_INVALID_LIST_POSITION;

  model = gtk_drop_down_get_model (drop_down);
  if (!G_IS_LIST_MODEL (model))
    return GTK_INVALID_LIST_POSITION;

  n = g_list_model_get_n_items (model);
  for (i = 0; i < n; i++)
    {
      g_autoptr (GtkStringObject) item = g_list_model_get_item (model, i);

      if (item &&
          g_strcmp0 (gtk_string_object_get_string (item), value) == 0)
        return i;
    }

  return GTK_INVALID_LIST_POSITION;
}

static char *
selected_string (GtkDropDown *drop_down)
{
  GListModel *model;
  guint index;
  g_autoptr (GtkStringObject) item = NULL;

  if (!GTK_IS_DROP_DOWN (drop_down))
    return NULL;

  model = gtk_drop_down_get_model (drop_down);
  if (!G_IS_LIST_MODEL (model))
    return NULL;

  index = gtk_drop_down_get_selected (drop_down);
  if (index == GTK_INVALID_LIST_POSITION)
    return NULL;

  item = g_list_model_get_item (model, index);
  return item ? g_strdup (gtk_string_object_get_string (item)) : NULL;
}

static void
set_dropdown_model (GtkDropDown   *drop_down,
                    GtkStringList *list,
                    const char    *select_value)
{
  guint selected = 0;

  g_return_if_fail (GTK_IS_DROP_DOWN (drop_down));
  g_return_if_fail (GTK_IS_STRING_LIST (list));

  gtk_drop_down_set_model (drop_down, G_LIST_MODEL (list));

  if (select_value && select_value[0] != '\0')
    {
      guint found = find_string (drop_down, select_value);

      if (found != GTK_INVALID_LIST_POSITION)
        selected = found;
    }

  gtk_drop_down_set_selected (drop_down, selected);
  g_object_unref (list);
}

static void
on_appearance_changed (GtkDropDown    *drop_down,
                       GParamSpec     *pspec G_GNUC_UNUSED,
                       OozeThemesPane *self)
{
  guint selected;

  if (self->filling)
    return;

  selected = gtk_drop_down_get_selected (drop_down);
  g_settings_set_string (self->interface_settings,
                         "color-scheme",
                         selected == 1 ? "prefer-dark" : "prefer-light");
}

static void
on_external_color_scheme_changed (GSettings      *settings,
                                  const char     *key G_GNUC_UNUSED,
                                  OozeThemesPane *self)
{
  g_autofree char *scheme = NULL;
  gboolean prev_filling;

  scheme = g_settings_get_string (settings, "color-scheme");

  /* Reflect the external change without echoing it back to GSettings. */
  prev_filling = self->filling;
  self->filling = TRUE;
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->appearance),
                              g_strcmp0 (scheme, "prefer-dark") == 0 ? 1 : 0);
  self->filling = prev_filling;
}

static void
on_icons_changed (GtkDropDown    *drop_down,
                  GParamSpec     *pspec G_GNUC_UNUSED,
                  OozeThemesPane *self)
{
  g_autofree char *name = NULL;

  if (self->filling)
    return;

  name = selected_string (drop_down);
  if (name)
    g_settings_set_string (self->interface_settings, "icon-theme", name);
}

static void
on_cursors_changed (GtkDropDown    *drop_down,
                    GParamSpec     *pspec G_GNUC_UNUSED,
                    OozeThemesPane *self)
{
  g_autofree char *name = NULL;

  if (self->filling)
    return;

  name = selected_string (drop_down);
  if (name)
    g_settings_set_string (self->interface_settings, "cursor-theme", name);
}

static void
on_cursor_size_changed (GtkSpinButton  *spin,
                        OozeThemesPane *self)
{
  if (!self->filling)
    g_settings_set_int (self->interface_settings,
                        "cursor-size",
                        gtk_spin_button_get_value_as_int (spin));
}

static void
on_foreign_gtk_changed (GtkDropDown    *drop_down,
                        GParamSpec     *pspec G_GNUC_UNUSED,
                        OozeThemesPane *self)
{
  g_autofree char *name = NULL;

  if (self->filling)
    return;

  name = selected_string (drop_down);
  if (!name || g_strcmp0 (name, "Automatic (WhiteSur)") == 0)
    ooze_foreign_gtk_pref_set (OOZE_FOREIGN_GTK_AUTO);
  else
    ooze_foreign_gtk_pref_set (name);
}

static void
ooze_themes_pane_dispose (GObject *object)
{
  OozeThemesPane *self = OOZE_THEMES_PANE (object);

  if (self->color_scheme_handler)
    {
      g_signal_handler_disconnect (self->interface_settings,
                                   self->color_scheme_handler);
      self->color_scheme_handler = 0;
    }
  g_clear_object (&self->interface_settings);
  G_OBJECT_CLASS (ooze_themes_pane_parent_class)->dispose (object);
}

static void
ooze_themes_pane_class_init (OozeThemesPaneClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_themes_pane_dispose;
}

static void
ooze_themes_pane_init (OozeThemesPane *self)
{
  const char *appearance_items[] = { "Light", "Dark", NULL };
  g_auto (GStrv) icons = NULL;
  g_auto (GStrv) cursors = NULL;
  g_auto (GStrv) foreign_themes = NULL;
  g_autofree char *icon_name = NULL;
  g_autofree char *cursor_name = NULL;
  g_autofree char *foreign_name = NULL;
  g_autofree char *scheme = NULL;
  GtkWidget *scroll;
  GtkWidget *surface;
  GtkWidget *box;
  GtkStringList *model;

  self->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);

  scroll = ooze_scrolled_window_new ();
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_box_append (GTK_BOX (self), scroll);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (surface, 16);
  gtk_widget_set_margin_end (surface, 16);
  gtk_widget_set_margin_top (surface, 16);
  gtk_widget_set_margin_bottom (surface, 16);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), surface);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_box_append (GTK_BOX (surface), box);

  self->filling = TRUE;

  /* Appearance: from_strings is safe. For dynamic theme lists, create an
   * empty GtkDropDown then gtk_drop_down_set_model() — constructing with a
   * live model and unreffing it early can fatal GTK list-item-manager
   * (Bail out! clear_model). Same pattern as Ooze Monitor. */
  self->appearance = gtk_drop_down_new_from_strings (appearance_items);
  scheme = g_settings_get_string (self->interface_settings, "color-scheme");
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->appearance),
                              g_strcmp0 (scheme, "prefer-dark") == 0 ? 1 : 0);
  self->color_scheme_handler =
    g_signal_connect (self->interface_settings,
                      "changed::color-scheme",
                      G_CALLBACK (on_external_color_scheme_changed),
                      self);
  gtk_box_append (GTK_BOX (box), make_row ("Appearance", self->appearance));

  /* Create empty dropdowns first, then attach models (Monitor pattern). */
  self->icons = gtk_drop_down_new (NULL, NULL);
  icons = ooze_icon_lookup_list_icon_themes ();
  icon_name = g_settings_get_string (self->interface_settings, "icon-theme");
  model = string_list_from_strv (icons, NULL);
  if (g_list_model_get_n_items (G_LIST_MODEL (model)) == 0)
    gtk_string_list_append (model, OOZE_ICON_THEME);
  set_dropdown_model (GTK_DROP_DOWN (self->icons), model, icon_name);
  gtk_box_append (GTK_BOX (box), make_row ("Icons", self->icons));

  self->cursors = gtk_drop_down_new (NULL, NULL);
  cursors = ooze_icon_lookup_list_cursor_themes ();
  cursor_name = g_settings_get_string (self->interface_settings, "cursor-theme");
  model = string_list_from_strv (cursors, NULL);
  if (g_list_model_get_n_items (G_LIST_MODEL (model)) == 0)
    gtk_string_list_append (model, OOZE_CURSOR_THEME);
  set_dropdown_model (GTK_DROP_DOWN (self->cursors), model, cursor_name);
  gtk_box_append (GTK_BOX (box), make_row ("Cursors", self->cursors));

  self->cursor_size = gtk_spin_button_new_with_range (16, 128, 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->cursor_size),
                             g_settings_get_int (self->interface_settings,
                                                 "cursor-size"));
  gtk_box_append (GTK_BOX (box), make_row ("Cursor Size", self->cursor_size));

  self->foreign_gtk = gtk_drop_down_new (NULL, NULL);
  foreign_themes = ooze_foreign_gtk_list_themes ();
  foreign_name = ooze_foreign_gtk_pref_get ();
  model = string_list_from_strv (foreign_themes, "Automatic (WhiteSur)");
  set_dropdown_model (GTK_DROP_DOWN (self->foreign_gtk),
                      model,
                      g_strcmp0 (foreign_name, OOZE_FOREIGN_GTK_AUTO) == 0
                        ? "Automatic (WhiteSur)"
                        : foreign_name);
  gtk_box_append (GTK_BOX (box),
                  make_row ("Foreign Apps (GTK)", self->foreign_gtk));

  g_signal_connect (self->appearance, "notify::selected",
                    G_CALLBACK (on_appearance_changed), self);
  g_signal_connect (self->icons, "notify::selected",
                    G_CALLBACK (on_icons_changed), self);
  g_signal_connect (self->cursors, "notify::selected",
                    G_CALLBACK (on_cursors_changed), self);
  g_signal_connect (self->cursor_size, "value-changed",
                    G_CALLBACK (on_cursor_size_changed), self);
  g_signal_connect (self->foreign_gtk, "notify::selected",
                    G_CALLBACK (on_foreign_gtk_changed), self);

  self->filling = FALSE;
}

GtkWidget *
ooze_themes_pane_new (void)
{
  return g_object_new (OOZE_TYPE_THEMES_PANE, NULL);
}
