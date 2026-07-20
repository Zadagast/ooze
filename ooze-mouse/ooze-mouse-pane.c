#include "ooze-mouse-pane.h"

#include "ooze-surface.h"

#include <gio/gio.h>

#define MOUSE_SCHEMA "org.gnome.desktop.peripherals.mouse"

struct _OozeMousePane
{
  GtkBox parent_instance;

  GSettings *settings;
  GtkWidget *profile_drop;
  GtkWidget *speed_scale;
  GtkWidget *natural_switch;
  gboolean   updating;
};

G_DEFINE_FINAL_TYPE (OozeMousePane, ooze_mouse_pane, GTK_TYPE_BOX)

static const char * const profile_values[] = { "default", "flat", "adaptive" };

static const char * const profile_labels[] = {
  "Default",
  "Flat (no acceleration)",
  "Adaptive",
  NULL,
};

static void
mouse_css_init (void)
{
  static gboolean loaded = FALSE;
  GtkCssProvider *provider;
  GdkDisplay *display;

  if (loaded)
    return;
  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (
    provider,
    ".mouse-rows {"
    "  background: alpha(@card_bg_color, 0.72);"
    "  border-radius: 8px;"
    "  box-shadow: inset 0 0 0 1px rgba(0,0,0,0.10);"
    "  padding: 0;"
    "}"
    ".mouse-rows > row {"
    "  padding: 10px 14px;"
    "  background: none;"
    "  border-radius: 0;"
    "}"
    ".mouse-rows > row:not(:last-child) {"
    "  border-bottom: 1px solid alpha(currentColor, 0.10);"
    "}"
    ".mouse-rows > row:first-child { border-radius: 8px 8px 0 0; }"
    ".mouse-rows > row:last-child { border-radius: 0 0 8px 8px; }"
    ".mouse-section {"
    "  font-weight: bold;"
    "  color: alpha(currentColor, 0.65);"
    "}"
    ".mouse-hint {"
    "  color: alpha(currentColor, 0.55);"
    "  font-size: 0.88em;"
    "}");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static guint
profile_index_from_settings (GSettings *settings)
{
  g_autofree char *value = g_settings_get_string (settings, "accel-profile");
  guint i;

  for (i = 0; i < G_N_ELEMENTS (profile_values); i++)
    {
      if (g_strcmp0 (value, profile_values[i]) == 0)
        return i;
    }
  return 0;
}

static void
on_profile_changed (GtkDropDown   *drop,
                    GParamSpec    *pspec G_GNUC_UNUSED,
                    OozeMousePane *self)
{
  guint selected;

  if (self->updating)
    return;

  selected = gtk_drop_down_get_selected (drop);
  if (selected >= G_N_ELEMENTS (profile_values))
    return;
  g_settings_set_string (self->settings, "accel-profile",
                         profile_values[selected]);
}

static void
on_speed_changed (GtkRange *range, OozeMousePane *self)
{
  if (self->updating)
    return;
  g_settings_set_double (self->settings, "speed",
                         gtk_range_get_value (range));
}

static void
on_natural_changed (GObject       *sw,
                    GParamSpec    *pspec G_GNUC_UNUSED,
                    OozeMousePane *self)
{
  if (self->updating)
    return;
  g_settings_set_boolean (self->settings, "natural-scroll",
                          gtk_switch_get_active (GTK_SWITCH (sw)));
}

static void
mouse_load_settings (OozeMousePane *self)
{
  self->updating = TRUE;
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->profile_drop),
                              profile_index_from_settings (self->settings));
  gtk_range_set_value (GTK_RANGE (self->speed_scale),
                       g_settings_get_double (self->settings, "speed"));
  gtk_switch_set_active (GTK_SWITCH (self->natural_switch),
                         g_settings_get_boolean (self->settings,
                                                 "natural-scroll"));
  self->updating = FALSE;
}

static void
on_settings_changed (GSettings     *settings G_GNUC_UNUSED,
                     const char    *key G_GNUC_UNUSED,
                     OozeMousePane *self)
{
  mouse_load_settings (self);
}

static GtkWidget *
make_row (const char *title, const char *subtitle, GtkWidget *control)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *labels;
  GtkWidget *label;

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

  labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand (labels, TRUE);
  gtk_widget_set_valign (labels, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), labels);

  label = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_append (GTK_BOX (labels), label);

  if (subtitle)
    {
      GtkWidget *sub = gtk_label_new (subtitle);
      gtk_label_set_xalign (GTK_LABEL (sub), 0.0);
      gtk_widget_add_css_class (sub, "mouse-hint");
      gtk_box_append (GTK_BOX (labels), sub);
    }

  gtk_widget_set_valign (control, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), control);

  return row;
}

static void
ooze_mouse_pane_dispose (GObject *object)
{
  OozeMousePane *self = OOZE_MOUSE_PANE (object);

  g_clear_object (&self->settings);
  G_OBJECT_CLASS (ooze_mouse_pane_parent_class)->dispose (object);
}

static void
ooze_mouse_pane_class_init (OozeMousePaneClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_mouse_pane_dispose;
}

static void
ooze_mouse_pane_init (OozeMousePane *self)
{
  GtkWidget *surface;
  GtkWidget *section;
  GtkWidget *rows;

  mouse_css_init ();

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_VERTICAL);

  self->settings = g_settings_new (MOUSE_SCHEMA);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);
  gtk_box_append (GTK_BOX (self), surface);

  section = gtk_label_new ("Pointer");
  gtk_label_set_xalign (GTK_LABEL (section), 0.0);
  gtk_widget_add_css_class (section, "mouse-section");
  gtk_widget_set_margin_top (section, 16);
  gtk_widget_set_margin_start (section, 18);
  gtk_widget_set_margin_bottom (section, 8);
  gtk_box_append (GTK_BOX (surface), section);

  rows = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (rows), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (rows, "mouse-rows");
  gtk_widget_set_margin_start (rows, 18);
  gtk_widget_set_margin_end (rows, 18);
  gtk_widget_set_valign (rows, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (surface), rows);

  self->profile_drop = gtk_drop_down_new_from_strings (profile_labels);
  gtk_widget_set_size_request (self->profile_drop, 200, -1);
  g_signal_connect (self->profile_drop, "notify::selected",
                    G_CALLBACK (on_profile_changed), self);
  gtk_list_box_append (
    GTK_LIST_BOX (rows),
    make_row ("Acceleration profile",
              "Flat moves the pointer a fixed amount per unit of "
              "mouse travel, like classic Mac OS and most games.",
              self->profile_drop));

  self->speed_scale =
    gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.05);
  gtk_scale_set_draw_value (GTK_SCALE (self->speed_scale), FALSE);
  gtk_scale_add_mark (GTK_SCALE (self->speed_scale), 0.0,
                      GTK_POS_BOTTOM, NULL);
  gtk_widget_set_size_request (self->speed_scale, 200, -1);
  g_signal_connect (self->speed_scale, "value-changed",
                    G_CALLBACK (on_speed_changed), self);
  gtk_list_box_append (GTK_LIST_BOX (rows),
                       make_row ("Pointer speed", NULL, self->speed_scale));

  self->natural_switch = gtk_switch_new ();
  g_signal_connect (self->natural_switch, "notify::active",
                    G_CALLBACK (on_natural_changed), self);
  gtk_list_box_append (
    GTK_LIST_BOX (rows),
    make_row ("Natural scrolling",
              "Scrolling moves the content, not the view.",
              self->natural_switch));

  mouse_load_settings (self);
  g_signal_connect (self->settings, "changed",
                    G_CALLBACK (on_settings_changed), self);
}

GtkWidget *
ooze_mouse_pane_new (void)
{
  return g_object_new (OOZE_MOUSE_TYPE_PANE, NULL);
}
