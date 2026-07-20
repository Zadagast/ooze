#include "ooze-feedback.h"

typedef struct
{
  GtkWidget *overlay;
  GtkWidget *toast;
} FeedbackTimeout;

static void
feedback_ensure_css (void)
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
    ".ooze-feedback {"
    "  padding: 8px 18px;"
    "  border-radius: 999px;"
    "  background: @accent_bg_color;"
    "  color: @accent_fg_color;"
    "  box-shadow: 0 3px 12px alpha(@window_fg_color, 0.24);"
    "  font-weight: 600;"
    "}");
  gtk_style_context_add_provider_for_display (
    display, GTK_STYLE_PROVIDER (provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  loaded = TRUE;
}

static void
feedback_timeout_free (FeedbackTimeout *timeout)
{
  g_object_unref (timeout->overlay);
  g_object_unref (timeout->toast);
  g_free (timeout);
}

static gboolean
feedback_timeout_cb (gpointer user_data)
{
  FeedbackTimeout *timeout = user_data;

  if (g_object_get_data (G_OBJECT (timeout->overlay), "ooze-feedback") ==
      timeout->toast)
    g_object_set_data (G_OBJECT (timeout->overlay), "ooze-feedback", NULL);
  gtk_widget_unparent (timeout->toast);
  feedback_timeout_free (timeout);
  return G_SOURCE_REMOVE;
}

void
ooze_feedback_show (GtkWindow  *parent,
                    const char *message)
{
  GtkWidget *content;
  GtkWidget *overlay;
  GtkWidget *old;
  GtkWidget *toast;
  FeedbackTimeout *timeout;

  g_return_if_fail (GTK_IS_WINDOW (parent));
  g_return_if_fail (message != NULL && message[0] != '\0');
  feedback_ensure_css ();

  content = gtk_window_get_child (parent);
  if (GTK_IS_OVERLAY (content))
    overlay = content;
  else
    {
      overlay = gtk_overlay_new ();
      if (content)
        {
          g_object_ref (content);
          gtk_window_set_child (parent, NULL);
          gtk_overlay_set_child (GTK_OVERLAY (overlay), content);
          g_object_unref (content);
        }
      gtk_window_set_child (parent, overlay);
    }

  old = g_object_get_data (G_OBJECT (overlay), "ooze-feedback");
  if (GTK_IS_WIDGET (old))
    {
      g_object_set_data (G_OBJECT (overlay), "ooze-feedback", NULL);
      gtk_widget_unparent (old);
    }

  toast = gtk_label_new (message);
  gtk_widget_add_css_class (toast, "ooze-feedback");
  gtk_widget_set_halign (toast, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (toast, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom (toast, 22);
  gtk_widget_set_can_target (toast, FALSE);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), toast);
  g_object_set_data (G_OBJECT (overlay), "ooze-feedback", toast);

  timeout = g_new0 (FeedbackTimeout, 1);
  timeout->overlay = g_object_ref (overlay);
  timeout->toast = g_object_ref (toast);
  g_timeout_add_full (G_PRIORITY_DEFAULT, 1800,
                      feedback_timeout_cb, timeout, NULL);
}
