#include "spot-priv.h"


typedef struct
{
  SpotWindow *window;
  GFile *directory;
  GFileEnumerator *enumerator;
  GCancellable *cancellable;
} SpotGridEnumeration;

static void spot_grid_enumeration_free (SpotGridEnumeration *enumeration);
static void spot_grid_enumerate_next_cb (GObject      *source,
                                         GAsyncResult *result,
                                         gpointer      user_data);
static void spot_grid_enumerate_children_cb (GObject      *source,
                                             GAsyncResult *result,
                                             gpointer      user_data);
gint spot_grid_sort_func (GtkFlowBoxChild *child_a,
                                 GtkFlowBoxChild *child_b,
                                 gpointer         user_data);

static GIcon *
spot_info_get_icon (GFileInfo *info)
{
  GIcon *icon;
  GFileType type;

  if (!info)
    return g_themed_icon_new ("text-x-generic");

  icon = g_file_info_get_icon (info);
  if (icon)
    return g_object_ref (icon);

  icon = g_file_info_get_symbolic_icon (info);
  if (icon)
    return g_object_ref (icon);

  if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
    {
      const char *content_type = g_file_info_get_content_type (info);

      if (content_type)
        return g_content_type_get_icon (content_type);
    }

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    return g_themed_icon_new ("folder");

  return g_themed_icon_new ("text-x-generic");
}

GtkWidget *
spot_image_new_for_info (GFileInfo *info)
{
  GtkWidget *image;
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkIconTheme *theme;

  image = gtk_image_new ();
  icon = spot_info_get_icon (info);
  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                              icon,
                                              SPOT_LIST_ICON_SIZE,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
  if (!paintable)
    paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                                icon,
                                                SPOT_LIST_ICON_SIZE,
                                                1,
                                                GTK_TEXT_DIR_LTR,
                                                GTK_ICON_LOOKUP_PRELOAD |
                                                GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));
  else
    gtk_image_set_from_gicon (GTK_IMAGE (image), icon);

  gtk_image_set_pixel_size (GTK_IMAGE (image), SPOT_LIST_ICON_SIZE);
  gtk_widget_set_size_request (image, SPOT_LIST_ICON_SIZE, SPOT_LIST_ICON_SIZE);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
  return image;
}

static GtkWidget *
spot_image_new_for_info_size (GFileInfo *info, int size)
{
  GtkWidget *image;
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkIconTheme *theme;

  image = gtk_image_new ();
  icon = spot_info_get_icon (info);
  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                              icon,
                                              size,
                                              1,
                                              GTK_TEXT_DIR_LTR,
                                              GTK_ICON_LOOKUP_PRELOAD);
  if (!paintable)
    paintable = gtk_icon_theme_lookup_by_gicon (theme,
                                                icon,
                                                size,
                                                1,
                                                GTK_TEXT_DIR_LTR,
                                                GTK_ICON_LOOKUP_PRELOAD |
                                                GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));
  else
    gtk_image_set_from_gicon (GTK_IMAGE (image), icon);

  gtk_image_set_pixel_size (GTK_IMAGE (image), size);
  gtk_widget_set_size_request (image, size, size);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (image, "ooze-icon");
  return image;
}

/* ── Grid-view helpers ──────────────────────────────────────────────────── */

static GtkWidget *
spot_create_grid_cell (SpotWindow *self, GFileInfo *info, GFile *file)
{
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *label;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_add_css_class (box, "spot-grid-cell");
  gtk_widget_set_size_request (box, SPOT_GRID_CELL_WIDTH, -1);
  gtk_widget_set_valign (box, GTK_ALIGN_START);
  /* Inner breathing room set here, not in CSS */
  gtk_widget_set_margin_top    (box, 3);
  gtk_widget_set_margin_bottom (box, 4);
  gtk_widget_set_margin_start  (box, 3);
  gtk_widget_set_margin_end    (box, 3);

  image = spot_image_new_for_info_size (info, SPOT_GRID_ICON_SIZE);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), image);

  label = gtk_label_new (g_file_info_get_display_name (info));
  gtk_widget_add_css_class (label, "spot-grid-label");
  gtk_label_set_xalign (GTK_LABEL (label), 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 12);
  gtk_label_set_lines (GTK_LABEL (label), 2);
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_widget_set_margin_top (label, 1);
  gtk_box_append (GTK_BOX (box), label);

  g_object_set_data_full (G_OBJECT (box), "spot-file",
                          g_object_ref (file), g_object_unref);
  spot_attach_file_drag (box, self, file);
  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_attach_folder_drop (box, self, file);
  return box;
}

static void
spot_clear_grid (SpotWindow *self)
{
  GtkWidget *child;

  /* Only remove real FlowBox children — ignore any stray non-item widgets. */
  child = gtk_widget_get_first_child (self->grid_flow);
  while (child != NULL)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);

      if (GTK_IS_FLOW_BOX_CHILD (child))
        gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), child);

      child = next;
    }
}

static void
spot_grid_enumeration_free (SpotGridEnumeration *enumeration)
{
  if (!enumeration)
    return;

  if (enumeration->window->grid_enumeration == enumeration)
    {
      enumeration->window->grid_enumeration = NULL;
      g_clear_object (&enumeration->window->reveal_target);
    }
  g_clear_object (&enumeration->enumerator);
  g_clear_object (&enumeration->directory);
  g_clear_object (&enumeration->cancellable);
  g_object_unref (enumeration->window);
  g_free (enumeration);
}

void
spot_grid_enumeration_cancel (SpotWindow *self)
{
  SpotGridEnumeration *enumeration = self->grid_enumeration;

  if (!enumeration)
    return;

  g_cancellable_cancel (enumeration->cancellable);
  self->grid_enumeration = NULL;
}

static void
spot_grid_append_info (SpotGridEnumeration *enumeration,
                       GFileInfo           *info)
{
  SpotWindow *self = enumeration->window;
  GtkWidget *loading;
  g_autoptr (GFile) child = NULL;
  GtkWidget *cell;
  GtkWidget *fbc;

  if (g_file_info_get_is_hidden (info))
    return;

  loading = gtk_widget_get_first_child (self->grid_flow);
  if (GTK_IS_FLOW_BOX_CHILD (loading))
    {
      GtkWidget *inner =
        gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (loading));

      if (inner && g_object_get_data (G_OBJECT (inner), "spot-loading"))
        gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), loading);
    }

  child = g_file_get_child (enumeration->directory,
                            g_file_info_get_name (info));
  cell = spot_create_grid_cell (self, info, child);
  g_object_set_data_full (G_OBJECT (cell), "spot-info",
                          g_object_ref (info), g_object_unref);
  gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), cell);

  fbc = gtk_widget_get_parent (cell);
  if (fbc)
    {
      gtk_widget_set_margin_top (fbc, 0);
      gtk_widget_set_margin_bottom (fbc, 0);
      gtk_widget_set_margin_start (fbc, 0);
      gtk_widget_set_margin_end (fbc, 0);
      gtk_widget_set_valign (fbc, GTK_ALIGN_START);
    }

  if (self->reveal_target)
    spot_select_file_widget (self, self->grid_flow, self->reveal_target);
}

gint
spot_grid_sort_func (GtkFlowBoxChild *child_a,
                     GtkFlowBoxChild *child_b,
                     gpointer         user_data G_GNUC_UNUSED)
{
  GtkWidget *widget_a;
  GtkWidget *widget_b;
  GFileInfo *info_a;
  GFileInfo *info_b;
  gboolean loading_a;
  gboolean loading_b;

  widget_a = gtk_flow_box_child_get_child (child_a);
  widget_b = gtk_flow_box_child_get_child (child_b);
  loading_a = widget_a &&
              g_object_get_data (G_OBJECT (widget_a), "spot-loading");
  loading_b = widget_b &&
              g_object_get_data (G_OBJECT (widget_b), "spot-loading");

  if (loading_a != loading_b)
    return loading_a ? -1 : 1;

  if (loading_a)
    return 0;

  info_a = widget_a ? g_object_get_data (G_OBJECT (widget_a), "spot-info") : NULL;
  info_b = widget_b ? g_object_get_data (G_OBJECT (widget_b), "spot-info") : NULL;
  if (!info_a || !info_b)
    return 0;

  return spot_compare_file_info (info_a, info_b);
}

static void
spot_grid_enumerate_next_cb (GObject      *source,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  SpotGridEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;
  GList *infos;
  GList *link;

  infos = g_file_enumerator_next_files_finish (
    G_FILE_ENUMERATOR (source), result, &error);
  if (error || !infos)
    {
      SpotWindow *self = enumeration->window;
      GtkWidget *first = gtk_widget_get_first_child (self->grid_flow);

      /* Enumeration is done — drop the loading placeholder, and give an
       * empty folder an intentional hint instead of a blank pane. */
      if (GTK_IS_FLOW_BOX_CHILD (first))
        {
          GtkWidget *inner =
            gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (first));

          if (inner && g_object_get_data (G_OBJECT (inner), "spot-loading"))
            {
              gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), first);
              first = gtk_widget_get_first_child (self->grid_flow);
            }
        }

      if (!first)
        {
          GtkWidget *hint = gtk_label_new ("Folder is empty");

          gtk_widget_add_css_class (hint, "spot-empty-hint");
          gtk_widget_set_margin_top (hint, 24);
          gtk_widget_set_margin_start (hint, 24);
          g_object_set_data (G_OBJECT (hint), "spot-loading",
                             GINT_TO_POINTER (1));
          gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), hint);
        }

      spot_grid_enumeration_free (enumeration);
      return;
    }

  for (link = infos; link; link = link->next)
    {
      spot_grid_append_info (enumeration, link->data);
      g_object_unref (link->data);
    }
  g_list_free (infos);

  g_file_enumerator_next_files_async (
    enumeration->enumerator, 32, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_next_cb, enumeration);
}

static void
spot_grid_enumerate_children_cb (GObject      *source,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  SpotGridEnumeration *enumeration = user_data;
  g_autoptr (GError) error = NULL;

  enumeration->enumerator =
    g_file_enumerate_children_finish (G_FILE (source), result, &error);
  if (error || !enumeration->enumerator)
    {
      spot_grid_enumeration_free (enumeration);
      return;
    }

  g_file_enumerator_next_files_async (
    enumeration->enumerator, 32, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_next_cb, enumeration);
}

void
spot_populate_grid (SpotWindow *self)
{
  SpotGridEnumeration *enumeration;

  spot_grid_enumeration_cancel (self);
  spot_clear_grid (self);

  if (!self->current_dir)
    return;

  {
    GtkWidget *loading = gtk_label_new ("Loading…");

    gtk_widget_set_margin_top (loading, 24);
    gtk_widget_set_margin_start (loading, 24);
    g_object_set_data (G_OBJECT (loading), "spot-loading",
                       GINT_TO_POINTER (1));
    gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), loading);
  }

  enumeration = g_new0 (SpotGridEnumeration, 1);
  enumeration->window = g_object_ref (self);
  enumeration->directory = g_object_ref (self->current_dir);
  enumeration->cancellable = g_cancellable_new ();
  self->grid_enumeration = enumeration;

  g_file_enumerate_children_async (
    enumeration->directory,
    G_FILE_ATTRIBUTE_STANDARD_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_ICON ","
    G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
    G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
    enumeration->cancellable, spot_grid_enumerate_children_cb, enumeration);
}

void
on_grid_child_activated (GtkFlowBox      *box G_GNUC_UNUSED,
                         GtkFlowBoxChild *child,
                         SpotWindow      *self)
{
  GtkWidget *cell = gtk_flow_box_child_get_child (child);
  GFile *file = g_object_get_data (G_OBJECT (cell), "spot-file");

  if (!file)
    return;

  g_autoptr (GFileInfo) info = g_file_query_info (
      file,
      G_FILE_ATTRIBUTE_STANDARD_TYPE,
      G_FILE_QUERY_INFO_NONE,
      NULL, NULL);

  if (info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}

/* ── List view (Finder list: sortable Name / Size / Modified) ──────────── */


