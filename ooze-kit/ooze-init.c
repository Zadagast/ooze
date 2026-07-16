#include "ooze-init.h"

#include "ooze-shared-icons.h"
#include "ooze-theme.h"

#include <adwaita.h>
#include <gtk/gtk.h>

void
ooze_kit_init (void)
{
  static gsize initialized;

  if (!g_once_init_enter (&initialized))
    return;

  gtk_init ();
  adw_init ();
  ooze_theme_ensure ();
  ooze_icons_configure_gtk_async ();

  g_once_init_leave (&initialized, 1);
}
