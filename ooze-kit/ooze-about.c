#include "ooze-about.h"

#include <adwaita.h>

void
ooze_about_present (GtkWindow  *parent,
                    const char *brand_name,
                    const char *icon_name,
                    const char *comments)
{
  AdwDialog *dialog;

  g_return_if_fail (GTK_IS_WINDOW (parent));
  g_return_if_fail (brand_name != NULL && brand_name[0] != '\0');

  dialog = adw_about_dialog_new ();
  adw_about_dialog_set_application_name (ADW_ABOUT_DIALOG (dialog), brand_name);
  adw_about_dialog_set_developer_name (ADW_ABOUT_DIALOG (dialog), "Ooze");

  if (icon_name && icon_name[0] != '\0')
    adw_about_dialog_set_application_icon (ADW_ABOUT_DIALOG (dialog), icon_name);

  if (comments && comments[0] != '\0')
    adw_about_dialog_set_comments (ADW_ABOUT_DIALOG (dialog), comments);

  adw_dialog_present (dialog, GTK_WIDGET (parent));
}
