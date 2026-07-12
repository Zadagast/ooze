#include "ooze-icons.h"

static gboolean
ooze_icon_name_is_symbolic (const char *name)
{
  return name && g_str_has_suffix (name, "-symbolic");
}

static gboolean
ooze_icon_paintable_is_usable (GtkIconPaintable *paintable)
{
  const char *name;

  if (!paintable)
    return FALSE;

  name = gtk_icon_paintable_get_icon_name (paintable);
  if (name && g_strcmp0 (name, "image-missing") == 0)
    return FALSE;

  return TRUE;
}

GtkIconPaintable *
ooze_icon_lookup (GtkIconTheme         *theme,
                  const char * const   *icon_names,
                  int                   icon_px)
{
  GtkIconPaintable *paintable;
  int i;

  if (!theme || !icon_names || icon_px <= 0)
    return NULL;

  /* 1) Named full-color icons that actually exist. */
  for (i = 0; icon_names[i]; i++)
    {
      if (ooze_icon_name_is_symbolic (icon_names[i]))
        continue;
      if (!gtk_icon_theme_has_icon (theme, icon_names[i]))
        continue;

      paintable = gtk_icon_theme_lookup_icon (theme,
                                              icon_names[i],
                                              NULL,
                                              icon_px,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD |
                                              GTK_ICON_LOOKUP_FORCE_REGULAR);
      if (ooze_icon_paintable_is_usable (paintable))
        return paintable;
      g_clear_object (&paintable);
    }

  /* 2) Explicit symbolic names (e.g. view-grid-symbolic). */
  for (i = 0; icon_names[i]; i++)
    {
      if (!ooze_icon_name_is_symbolic (icon_names[i]))
        continue;
      if (!gtk_icon_theme_has_icon (theme, icon_names[i]))
        continue;

      paintable = gtk_icon_theme_lookup_icon (theme,
                                              icon_names[i],
                                              NULL,
                                              icon_px,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
      if (ooze_icon_paintable_is_usable (paintable))
        return paintable;
      g_clear_object (&paintable);
    }

  /* 3) Any remaining listed name the theme knows about. */
  for (i = 0; icon_names[i]; i++)
    {
      if (!gtk_icon_theme_has_icon (theme, icon_names[i]))
        continue;

      paintable = gtk_icon_theme_lookup_icon (theme,
                                              icon_names[i],
                                              NULL,
                                              icon_px,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
      if (ooze_icon_paintable_is_usable (paintable))
        return paintable;
      g_clear_object (&paintable);
    }

  return NULL;
}

GtkWidget *
ooze_icon_image_new (const char * const *icon_names,
                     int                 icon_px)
{
  GtkIconTheme *theme;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkWidget *image;

  if (icon_px <= 0)
    icon_px = OOZE_ICON_SIZE_TOOLBAR;

  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  paintable = ooze_icon_lookup (theme, icon_names, icon_px);

  image = gtk_image_new ();
  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));
  else
    gtk_image_set_from_icon_name (GTK_IMAGE (image), "image-missing");

  /* Fixed square viewport — keeps mixed SVGs optically aligned. */
  gtk_image_set_pixel_size (GTK_IMAGE (image), icon_px);
  gtk_widget_set_size_request (image, icon_px, icon_px);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (image, "ooze-icon");

  return image;
}
