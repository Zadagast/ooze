#include "ooze-scroll.h"

#include "ooze-palette.h"
#include "ooze-theme.h"

#include <adwaita.h>

static GtkCssProvider *scroll_provider = NULL;

static void
ooze_scroll_load_css (void)
{
  const OozePalette *pal;
  const char *css;

  if (!scroll_provider)
    return;

  pal = ooze_palette_current ();

  /* Light / dark trough + glass thumb — Adwaita does not put .dark on
   * widgets, so we swap the whole sheet on notify::dark instead.
   *
   * Must use Adwaita's full selectors and zero trough margins; otherwise
   * margin-left/right: 8px and the slider's 8px transparent border survive
   * and leave a fat gutter beside always-visible bars.
   */
  if (pal->dark)
    css =
      "scrollbar {"
      "  padding: 0;"
      "}"
      "scrollbar.vertical {"
      "  min-width: 15px;"
      "}"
      "scrollbar.horizontal {"
      "  min-height: 15px;"
      "}"

      "scrollbar > range > trough {"
      "  margin: 0;"
      "  min-width: 15px;"
      "  min-height: 15px;"
      "  border-radius: 0;"
      "  background-color: #2a2a2e;"
      "  box-shadow:"
      "    inset 1px 0 0 rgba(0,0,0,0.55),"
      "    inset -1px 0 0 rgba(255,255,255,0.06),"
      "    inset 0 1px 0 rgba(0,0,0,0.40);"
      "}"
      "scrollbar.vertical > range > trough {"
      "  margin-left: 0;"
      "  margin-right: 0;"
      "}"
      "scrollbar.horizontal > range > trough {"
      "  margin-top: 0;"
      "  margin-bottom: 0;"
      "  box-shadow:"
      "    inset 0 1px 0 rgba(0,0,0,0.55),"
      "    inset 0 -1px 0 rgba(255,255,255,0.06),"
      "    inset 1px 0 0 rgba(0,0,0,0.35);"
      "}"

      "scrollbar > range > trough > slider {"
      "  min-width: 13px;"
      "  min-height: 24px;"
      "  margin: 1px;"
      "  border-radius: 4px;"
      "  border: 1px solid rgba(0,0,0,0.55);"
      "  background-clip: border-box;"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #4a4a52 0%,"
      "    #3a3a42 50%,"
      "    #2e2e34 100%);"
      "  box-shadow:"
      "    inset 0 1px 0 rgba(255,255,255,0.14),"
      "    0 1px 1px rgba(0,0,0,0.35);"
      "}"
      "scrollbar.horizontal > range > trough > slider {"
      "  min-width: 24px;"
      "  min-height: 13px;"
      "  background-image: linear-gradient("
      "    to right,"
      "    #4a4a52 0%,"
      "    #3a3a42 50%,"
      "    #2e2e34 100%);"
      "}"
      "scrollbar > range > trough > slider:hover {"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #5a5a64 0%,"
      "    #44444c 50%,"
      "    #36363c 100%);"
      "}"
      "scrollbar.horizontal > range > trough > slider:hover {"
      "  background-image: linear-gradient("
      "    to right,"
      "    #5a5a64 0%,"
      "    #44444c 50%,"
      "    #36363c 100%);"
      "}"
      "scrollbar > range > trough > slider:active {"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #323238 0%,"
      "    #28282e 100%);"
      "  box-shadow: inset 0 1px 2px rgba(0,0,0,0.45);"
      "}"

      "scrollbar.overlay-indicator,"
      "scrollbar.overlay-indicator.hovering,"
      "scrollbar.overlay-indicator.dragging {"
      "  opacity: 1;"
      "  background: none;"
      "}"

      ".ooze-scrolled { background: none; }";
  else
    css =
      "scrollbar {"
      "  padding: 0;"
      "}"
      "scrollbar.vertical {"
      "  min-width: 15px;"
      "}"
      "scrollbar.horizontal {"
      "  min-height: 15px;"
      "}"

      "scrollbar > range > trough {"
      "  margin: 0;"
      "  min-width: 15px;"
      "  min-height: 15px;"
      "  border-radius: 0;"
      "  background-color: #d0d0d0;"
      "  box-shadow:"
      "    inset 1px 0 0 rgba(0,0,0,0.18),"
      "    inset -1px 0 0 rgba(255,255,255,0.35),"
      "    inset 0 1px 0 rgba(0,0,0,0.12);"
      "}"
      "scrollbar.vertical > range > trough {"
      "  margin-left: 0;"
      "  margin-right: 0;"
      "}"
      "scrollbar.horizontal > range > trough {"
      "  margin-top: 0;"
      "  margin-bottom: 0;"
      "  box-shadow:"
      "    inset 0 1px 0 rgba(0,0,0,0.18),"
      "    inset 0 -1px 0 rgba(255,255,255,0.35),"
      "    inset 1px 0 0 rgba(0,0,0,0.10);"
      "}"

      "scrollbar > range > trough > slider {"
      "  min-width: 13px;"
      "  min-height: 24px;"
      "  margin: 1px;"
      "  border-radius: 4px;"
      "  border: 1px solid rgba(0,0,0,0.28);"
      "  background-clip: border-box;"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #f6f6f8 0%,"
      "    #e4e4e8 45%,"
      "    #c8c8d0 100%);"
      "  box-shadow:"
      "    inset 0 1px 0 rgba(255,255,255,0.85),"
      "    0 1px 1px rgba(0,0,0,0.12);"
      "}"
      "scrollbar.horizontal > range > trough > slider {"
      "  min-width: 24px;"
      "  min-height: 13px;"
      "  background-image: linear-gradient("
      "    to right,"
      "    #f6f6f8 0%,"
      "    #e4e4e8 45%,"
      "    #c8c8d0 100%);"
      "}"
      "scrollbar > range > trough > slider:hover {"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #ffffff 0%,"
      "    #ececf0 45%,"
      "    #d2d2da 100%);"
      "}"
      "scrollbar.horizontal > range > trough > slider:hover {"
      "  background-image: linear-gradient("
      "    to right,"
      "    #ffffff 0%,"
      "    #ececf0 45%,"
      "    #d2d2da 100%);"
      "}"
      "scrollbar > range > trough > slider:active {"
      "  background-image: linear-gradient("
      "    to bottom,"
      "    #d8d8e0 0%,"
      "    #c0c0c8 100%);"
      "  box-shadow: inset 0 1px 2px rgba(0,0,0,0.18);"
      "}"

      "scrollbar.overlay-indicator,"
      "scrollbar.overlay-indicator.hovering,"
      "scrollbar.overlay-indicator.dragging {"
      "  opacity: 1;"
      "  background: none;"
      "}"

      ".ooze-scrolled { background: none; }";

  gtk_css_provider_load_from_string (scroll_provider, css);
}

static void
on_dark_changed (GObject    *obj G_GNUC_UNUSED,
                 GParamSpec *pspec G_GNUC_UNUSED,
                 gpointer    user_data G_GNUC_UNUSED)
{
  ooze_scroll_load_css ();
}

void
ooze_scroll_ensure_css (void)
{
  GdkDisplay *display;

  if (scroll_provider)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  scroll_provider = gtk_css_provider_new ();
  ooze_scroll_load_css ();

  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (scroll_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 20);

  ooze_theme_connect_dark_notify_full (NULL,
                                       G_CALLBACK (on_dark_changed),
                                       FALSE);
}

GtkWidget *
ooze_scrolled_window_new (void)
{
  GtkWidget *scrolled;

  ooze_scroll_ensure_css ();

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scrolled), FALSE);
  gtk_widget_add_css_class (scrolled, "ooze-scrolled");
  return scrolled;
}
