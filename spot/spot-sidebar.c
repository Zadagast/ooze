#include "spot-priv.h"

const char * const spot_icon_home[] = {
  "user-home", "go-home", NULL
};
const char * const spot_icon_applications[] = {
  "application-default-icon", "application-x-executable", "system-run", NULL
};
static const char * const spot_icon_drive[] = {
  "drive-harddisk", "drive-harddisk-symbolic", NULL
};

const SpotPlace spot_sidebar_places[] = {
  { "Home", 0, TRUE },
  { "Desktop", G_USER_DIRECTORY_DESKTOP, FALSE },
  { "Documents", G_USER_DIRECTORY_DOCUMENTS, FALSE },
  { "Downloads", G_USER_DIRECTORY_DOWNLOAD, FALSE },
  { "Pictures", G_USER_DIRECTORY_PICTURES, FALSE },
  { "Music", G_USER_DIRECTORY_MUSIC, FALSE },
  { "Videos", G_USER_DIRECTORY_VIDEOS, FALSE },
};
const guint spot_sidebar_places_len = G_N_ELEMENTS (spot_sidebar_places);

/*
 * Resolve a sidebar / Go-menu place path. Prefer XDG user-dirs; if that fails
 * (common when the nest remaps XDG_CONFIG_HOME without user-dirs.dirs), fall
 * back to $HOME/<label> so Documents / Downloads / etc. still appear.
 */
char *
spot_place_path_dup (const SpotPlace *place)
{
  const char *path;

  if (place->use_home)
    return g_strdup (g_get_home_dir ());

  path = g_get_user_special_dir (place->dir);
  if (path && path[0] != '\0')
    return g_strdup (path);

  return g_build_filename (g_get_home_dir (), place->label, NULL);
}

char *
spot_special_dir_path_dup (GUserDirectory dir,
                           const char    *fallback_name)
{
  const char *path = g_get_user_special_dir (dir);

  if (path && path[0] != '\0')
    return g_strdup (path);

  return g_build_filename (g_get_home_dir (), fallback_name, NULL);
}

/* ══════════════════════════════════════════════════════════════════════════ */


static const char * const *
spot_sidebar_icon_names (const SpotPlace *place)
{
  if (place->use_home)
    return spot_icon_home;

  switch (place->dir)
    {
    case G_USER_DIRECTORY_DESKTOP:
      {
        static const char * const icons[] = { "user-desktop", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_DOCUMENTS:
      {
        static const char * const icons[] = { "folder-documents", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_DOWNLOAD:
      {
        static const char * const icons[] = { "folder-download", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_PICTURES:
      {
        static const char * const icons[] = { "folder-pictures", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_MUSIC:
      {
        static const char * const icons[] = { "folder-music", "folder", NULL };
        return icons;
      }
    case G_USER_DIRECTORY_VIDEOS:
      {
        static const char * const icons[] = { "folder-videos", "folder", NULL };
        return icons;
      }
    default:
      {
        static const char * const icons[] = { "folder", NULL };
        return icons;
      }
    }
}


static GtkWidget *
spot_create_sidebar_row (const char          *label,
                         const char          *path,
                         const char * const *icon_names)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *lbl;

  row = gtk_list_box_row_new ();
  /* Compact Finder row — icon beside label so Favorites + Locations all
   * fit on screen without scrolling the sidebar. */
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 7);
  image = spot_image_new_from_icon_list (icon_names, SPOT_SIDEBAR_ICON_SIZE, TRUE);
  lbl = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (lbl), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class (lbl, "spot-sidebar-label");
  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), lbl);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
  g_object_set_data_full (G_OBJECT (row),
                          "spot-path",
                          g_strdup (path),
                          g_free);
  return row;
}

/* Finder-style section heading ("Favorites" / "Locations"). */
static GtkWidget *
spot_create_sidebar_heading (const char *label)
{
  GtkWidget *row;
  GtkWidget *lbl;
  g_autofree char *upper = g_utf8_strup (label, -1);

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_add_css_class (row, "spot-sidebar-heading");
  lbl = gtk_label_new (upper);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.0);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), lbl);
  return row;
}


static void
on_sidebar_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                          GtkListBoxRow *row,
                          gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  const char *path = g_object_get_data (G_OBJECT (row), "spot-path");

  if (g_object_get_data (G_OBJECT (row), "spot-launch-pak"))
    {
      spot_launch_pak ();
      return;
    }

  if (path)
    spot_navigate_to_path_string (self, path, TRUE);
}

GtkWidget *
spot_create_sidebar (SpotWindow *self)
{
  GtkWidget *bin;
  GtkWidget *scrolled;
  GtkWidget *list;
  gsize i;

  /* OozeSurface draws the sidebar chrome; the scrolled window inside is
   * transparent so the surface colour shows through. */
  bin = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (bin, 148, -1);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  /* CSS class on the list so row selection/hover selectors still work */
  gtk_widget_add_css_class (list, "spot-sidebar-list");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);
  gtk_box_append (GTK_BOX (bin), scrolled);

  gtk_list_box_append (GTK_LIST_BOX (list),
                       spot_create_sidebar_heading ("Favorites"));

  for (i = 0; i < spot_sidebar_places_len; i++)
    {
      g_autofree char *path = spot_place_path_dup (&spot_sidebar_places[i]);

      if (!path)
        continue;

      gtk_list_box_append (GTK_LIST_BOX (list),
                           spot_create_sidebar_row (spot_sidebar_places[i].label,
                                                    path,
                                                    spot_sidebar_icon_names (&spot_sidebar_places[i])));
    }

  {
    GtkWidget *apps = spot_create_sidebar_row ("Applications", NULL,
                                               spot_icon_applications);
    g_object_set_data (G_OBJECT (apps), "spot-launch-pak", GINT_TO_POINTER (1));
    gtk_list_box_append (GTK_LIST_BOX (list), apps);
  }

  gtk_list_box_append (GTK_LIST_BOX (list),
                       spot_create_sidebar_heading ("Locations"));
  gtk_list_box_append (GTK_LIST_BOX (list),
                       spot_create_sidebar_row ("Linux HD",
                                                "/",
                                                spot_icon_drive));

  g_signal_connect (list, "row-activated", G_CALLBACK (on_sidebar_row_activated), self);
  self->sidebar = list;

  return bin;
}

/* ── Folder-scoped search (filters the current views in place) ──────── */


