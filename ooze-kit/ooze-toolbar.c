#include "ooze-toolbar.h"
#include "ooze-surface.h"
#include "ooze-theme.h"
#include "aqua-chrome.h"

#include <gtk/gtk.h>

/*
 * MAIN BAR metrics (OozeKit strip under the Ooze Gel title bar)
 * ─────────────────────────────────────────────────────────────
 * Spacing matches libadwaita .toolbar:
 *   padding: 6px; border-spacing: 6px;
 * (verified in libadwaita-1.so / upstream _header-bar.scss).
 *
 * Apps compose this with ooze_toolbar_new() + add_group / separator /
 * spacer + ooze_button_new_toolbar(). Height is NOT forced — the strip
 * sizes to its tallest tile's natural content (icon + caption + button
 * pad) plus the shell padding. OOZE_TOOLBAR_HEIGHT in aqua-chrome.h is
 * a nominal reference only. Do not invent per-app padding — change the
 * numbers here instead. Stay flush with Ooze Gel (no outer surface margin).
 */
#define OOZE_TOOLBAR_GROUP_SPACING 6

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
    /*
     * Must beat .ooze-surface { padding: 0 }. Dual class keeps MAIN BAR
     * Adwaita .toolbar density intact.
     */
    ".ooze-surface.ooze-toolbar {"
    "  background: none;"
    "  padding: 6px;"           /* libadwaita .toolbar */
    "  border-spacing: 6px;"    /* libadwaita .toolbar */
    "}"
    ".ooze-toolbar-group {"
    "  padding: 0;"
    "}"
    ".ooze-toolbar separator {"
    "  background: @borders;"
    "  min-width: 1px;"
    "  margin: 0 6px;"
    "}"
    /* Equal tile footprint for MAIN BAR symmetry (40px glyphs + captions). */
    ".ooze-toolbar-btn {"
    "  min-width: 76px;"
    "}"
    ".ooze-toolbar-btn:active { color: @accent_fg_color; }"
    /* Trailing search / accessory — optical middle of the bar. */
    ".ooze-toolbar-search {"
    "  min-width: 120px;"
    "  min-height: 32px;"
    "  border-radius: 10px;"
    "  margin: 0 4px;"
    "}"
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
  /* Spacing comes from CSS border-spacing (Adwaita .toolbar). */
  gtk_box_set_spacing (GTK_BOX (toolbar), 0);
  gtk_widget_set_valign (toolbar, GTK_ALIGN_FILL);
  /* Let button glass rims paint into the 6px padding (not clipped). */
  gtk_widget_set_overflow (toolbar, GTK_OVERFLOW_VISIBLE);
  /* No forced height — size to real tile content (see header comment). */
  /* Flush to neighbors so Ooze Gel pinlines continue (no outer margin). */
  gtk_widget_add_css_class (toolbar, "ooze-toolbar");
  return toolbar;
}

GtkWidget *
ooze_toolbar_add_group (GtkWidget *toolbar)
{
  GtkWidget *group;

  g_return_val_if_fail (GTK_IS_BOX (toolbar), NULL);

  group = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, OOZE_TOOLBAR_GROUP_SPACING);
  gtk_widget_add_css_class (group, "ooze-toolbar-group");
  gtk_widget_set_valign (group, GTK_ALIGN_CENTER);
  gtk_widget_set_overflow (group, GTK_OVERFLOW_VISIBLE);
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
