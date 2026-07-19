#include "ooze-action-bar.h"

void
ooze_action_bar_ensure_css (void)
{
  static gboolean loaded;
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
    ".ooze-action-bar {"
    "  padding: 8px 14px;"
    "  background: alpha(@card_bg_color, 0.72);"
    "  box-shadow: inset 0 1px 0 alpha(@window_fg_color, 0.10);"
    "}"
    ".ooze-action-bar button {"
    "  min-width: 84px;"
    "  padding: 5px 16px;"
    "}");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

GtkWidget *
ooze_action_bar_new (GtkWidget **out_cancel,
                     GtkWidget **out_apply)
{
  GtkWidget *bar;
  GtkWidget *spacer;
  GtkWidget *cancel;
  GtkWidget *apply;

  ooze_action_bar_ensure_css ();

  bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class (bar, "ooze-action-bar");

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (bar), spacer);

  cancel = gtk_button_new_with_label ("Cancel");
  gtk_box_append (GTK_BOX (bar), cancel);

  apply = gtk_button_new_with_label ("Apply");
  gtk_widget_add_css_class (apply, "suggested-action");
  gtk_box_append (GTK_BOX (bar), apply);

  g_object_set_data (G_OBJECT (bar), "ooze-action-cancel", cancel);
  g_object_set_data (G_OBJECT (bar), "ooze-action-apply", apply);
  ooze_action_bar_set_dirty (bar, FALSE);

  if (out_cancel)
    *out_cancel = cancel;
  if (out_apply)
    *out_apply = apply;
  return bar;
}

void
ooze_action_bar_set_dirty (GtkWidget *bar,
                           gboolean   dirty)
{
  GtkWidget *cancel;
  GtkWidget *apply;

  g_return_if_fail (GTK_IS_WIDGET (bar));

  cancel = g_object_get_data (G_OBJECT (bar), "ooze-action-cancel");
  apply = g_object_get_data (G_OBJECT (bar), "ooze-action-apply");
  if (cancel)
    gtk_widget_set_sensitive (cancel, dirty);
  if (apply)
    gtk_widget_set_sensitive (apply, dirty);
}
