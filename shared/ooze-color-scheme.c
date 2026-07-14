#include "ooze-color-scheme.h"

gboolean
ooze_color_scheme_is_dark (GSettings *settings)
{
  g_autofree char *scheme = NULL;

  if (!settings)
    return FALSE;

  scheme = g_settings_get_string (settings, "color-scheme");
  return g_strcmp0 (scheme, "prefer-dark") == 0;
}
