#include "ooze-toolbar.h"
#include "ooze-surface.h"
#include "ooze-theme.h"

#include <gtk/gtk.h>

void
ooze_toolbar_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay *display;
  GtkCssProvider *p;

  if (loaded)
    return;

  ooze_theme_ensure ();

  display = gdk_display_get_default ();
  if (!display)
    return;

  p = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (p,
    ".ooze-toolbar {"
    "  background: none;"
    "  padding: 4px 8px;"
    "}"
    ".ooze-toolbar-group {"
    "  padding: 0 2px;"
    "}"
    ".ooze-toolbar separator {"
    "  background: @borders;"
    "  min-width: 1px;"
    "  margin: 6px 4px;"
    "}"
    /* Width floor only — height comes from labeled tiles. */
    ".ooze-toolbar-btn {"
    "  min-width: 48px;"
    "}"
    ".ooze-toolbar-btn:active { color: #ffffff; }"
    ".ooze-settings-tile {"
    "  min-width: 112px;"
    "  min-height: 96px;"
    "  padding: 10px 8px;"
    "}"
    ".ooze-settings-tile .ooze-settings-tile-sub {"
    "  opacity: 0.72;"
    "}"
    ".ooze-settings-grid {"
    "  padding: 8px 16px 24px 16px;"
    "}"
    /* King system-app launcher tiles: large color icon + caption. */
    ".ooze-launcher-tile {"
    "  min-width: 128px;"
    "  min-height: 120px;"
    "  padding: 14px 12px;"
    "}"
    ".ooze-launcher-tile .ooze-button-label {"
    "  font-size: 11pt;"
    "  margin-top: 4px;"
    "}"
    ".ooze-launcher-grid {"
    "  padding: 28px 24px 36px 24px;"
    "}"
    ".ooze-toolbar-btn:disabled,"
    ".ooze-nav-btn:disabled {"
    "  opacity: 0.42;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (p);
  loaded = TRUE;
}

GtkWidget *
ooze_toolbar_new (void)
{
  GtkWidget *toolbar;

  ooze_toolbar_ensure_css ();
  toolbar = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (toolbar), 0);
  gtk_widget_add_css_class (toolbar, "ooze-toolbar");
  return toolbar;
}

GtkWidget *
ooze_toolbar_add_group (GtkWidget *toolbar)
{
  GtkWidget *group;

  g_return_val_if_fail (GTK_IS_BOX (toolbar), NULL);

  group = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_widget_add_css_class (group, "ooze-toolbar-group");
  gtk_widget_set_valign (group, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (toolbar), group);
  return group;
}

GtkWidget *
ooze_toolbar_add_separator (GtkWidget *toolbar)
{
  GtkWidget *sep;

  g_return_val_if_fail (GTK_IS_BOX (toolbar), NULL);

  sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_valign (sep, GTK_ALIGN_FILL);
  gtk_box_append (GTK_BOX (toolbar), sep);
  return sep;
}

GtkWidget *
ooze_toolbar_add_spacer (GtkWidget *toolbar)
{
  GtkWidget *spacer;

  g_return_val_if_fail (GTK_IS_BOX (toolbar), NULL);

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (toolbar), spacer);
  return spacer;
}
