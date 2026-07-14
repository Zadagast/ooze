/*
 * Event-driven window tracker, modelled on GNOME Shell's ShellWindowTracker.
 *
 * Watches window creation/removal and app-identity changes (wm-class and
 * gtk-application-id often arrive after the window is created) and coalesces
 * them into a single changed notification per main-loop iteration, so
 * consumers such as the dock can react to window events instead of polling.
 */

#include "ooze-window-tracker.h"

#include <meta/window.h>

typedef struct
{
  OozeWindowTracker *tracker;
  gulong unmanaged_id;
  gulong wm_class_id;
  gulong gtk_app_id_id;
} OozeWindowWatch;

struct _OozeWindowTracker
{
  MetaDisplay *display; /* borrowed */
  gulong window_created_id;
  GHashTable *watches; /* MetaWindow* -> OozeWindowWatch* */
  guint notify_idle;

  OozeWindowTrackerChangedFn changed_cb;
  gpointer changed_data;
};

static gboolean
ooze_window_tracker_notify_idle (gpointer user_data)
{
  OozeWindowTracker *tracker = user_data;

  tracker->notify_idle = 0;
  if (tracker->changed_cb)
    tracker->changed_cb (tracker->changed_data);
  return G_SOURCE_REMOVE;
}

static void
ooze_window_tracker_schedule_notify (OozeWindowTracker *tracker)
{
  if (tracker->notify_idle)
    return;

  tracker->notify_idle = g_idle_add (ooze_window_tracker_notify_idle, tracker);
}

static void
ooze_window_watch_disconnect (gpointer key,
                              gpointer value,
                              gpointer user_data G_GNUC_UNUSED)
{
  MetaWindow *window = key;
  OozeWindowWatch *watch = value;

  g_clear_signal_handler (&watch->unmanaged_id, window);
  g_clear_signal_handler (&watch->wm_class_id, window);
  g_clear_signal_handler (&watch->gtk_app_id_id, window);
}

static void
on_window_identity_changed (MetaWindow *window G_GNUC_UNUSED,
                            GParamSpec *pspec G_GNUC_UNUSED,
                            gpointer    user_data)
{
  OozeWindowWatch *watch = user_data;

  ooze_window_tracker_schedule_notify (watch->tracker);
}

static void
on_window_unmanaged (MetaWindow *window,
                     gpointer    user_data)
{
  OozeWindowWatch *watch = user_data;
  OozeWindowTracker *tracker = watch->tracker;

  ooze_window_watch_disconnect (window, watch, NULL);
  g_hash_table_remove (tracker->watches, window);
  ooze_window_tracker_schedule_notify (tracker);
}

static void
ooze_window_tracker_watch_window (OozeWindowTracker *tracker,
                                  MetaWindow        *window)
{
  OozeWindowWatch *watch;

  if (g_hash_table_contains (tracker->watches, window))
    return;

  watch = g_new0 (OozeWindowWatch, 1);
  watch->tracker = tracker;
  watch->unmanaged_id =
    g_signal_connect (window, "unmanaged",
                      G_CALLBACK (on_window_unmanaged), watch);
  watch->wm_class_id =
    g_signal_connect (window, "notify::wm-class",
                      G_CALLBACK (on_window_identity_changed), watch);
  watch->gtk_app_id_id =
    g_signal_connect (window, "notify::gtk-application-id",
                      G_CALLBACK (on_window_identity_changed), watch);
  g_hash_table_insert (tracker->watches, window, watch);
}

static void
on_window_created (MetaDisplay *display G_GNUC_UNUSED,
                   MetaWindow  *window,
                   gpointer     user_data)
{
  OozeWindowTracker *tracker = user_data;

  ooze_window_tracker_watch_window (tracker, window);
  ooze_window_tracker_schedule_notify (tracker);
}

OozeWindowTracker *
ooze_window_tracker_new (MetaDisplay *display)
{
  OozeWindowTracker *tracker;
  GList *windows;
  GList *l;

  g_return_val_if_fail (display != NULL, NULL);

  tracker = g_new0 (OozeWindowTracker, 1);
  tracker->display = display;
  tracker->watches = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  tracker->window_created_id =
    g_signal_connect (display, "window-created",
                      G_CALLBACK (on_window_created), tracker);

  windows = meta_display_list_all_windows (display);
  for (l = windows; l != NULL; l = l->next)
    ooze_window_tracker_watch_window (tracker, l->data);
  g_list_free (windows);

  return tracker;
}

void
ooze_window_tracker_free (OozeWindowTracker *tracker)
{
  if (!tracker)
    return;

  if (tracker->notify_idle)
    {
      g_source_remove (tracker->notify_idle);
      tracker->notify_idle = 0;
    }

  g_clear_signal_handler (&tracker->window_created_id, tracker->display);
  g_hash_table_foreach (tracker->watches, ooze_window_watch_disconnect, NULL);
  g_hash_table_destroy (tracker->watches);
  g_free (tracker);
}

void
ooze_window_tracker_set_changed_callback (OozeWindowTracker         *tracker,
                                          OozeWindowTrackerChangedFn callback,
                                          gpointer                   user_data)
{
  g_return_if_fail (tracker != NULL);

  tracker->changed_cb = callback;
  tracker->changed_data = user_data;
}
