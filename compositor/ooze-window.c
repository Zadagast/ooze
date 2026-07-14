#include "ooze-window.h"

#include <meta/window.h>

/*
 * Window move/resize/edge-tile are Mutter-owned (MetaFrames + client CSD +
 * edge-tiling prefs), matching GNOME Shell. Ooze used to inject Clutter grab
 * overlays into window_group; that fought MetaFrames/CSD and crashed Mutter.
 */

gboolean
ooze_window_is_client_decorated (MetaWindow *window)
{
  if (!window)
    return FALSE;

  if (meta_window_get_client_type (window) == META_WINDOW_CLIENT_TYPE_WAYLAND)
    return TRUE;

  if (meta_window_get_gtk_application_id (window) != NULL)
    return TRUE;

  return FALSE;
}

gboolean
ooze_window_uses_ooze_client_chrome (MetaWindow *window)
{
  const char *app_id;

  if (!window)
    return FALSE;

  app_id = meta_window_get_gtk_application_id (window);
  return app_id && g_str_has_prefix (app_id, "org.ooze.");
}

void
ooze_window_setup (MetaWindowActor *actor G_GNUC_UNUSED)
{
  /* Intentionally empty — Mutter MetaFrames / client CSD own grips. */
}

void
ooze_window_sync (MetaWindowActor *actor G_GNUC_UNUSED)
{
}

void
ooze_window_schedule_sync (MetaWindowActor *actor G_GNUC_UNUSED)
{
}

void
ooze_window_cancel_scheduled_sync (MetaWindowActor *actor G_GNUC_UNUSED)
{
}

void
ooze_window_teardown (MetaWindowActor *actor G_GNUC_UNUSED)
{
}
