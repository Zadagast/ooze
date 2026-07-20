#include "spot-priv.h"

static void
spot_list_name_setup (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                      GtkListItem              *item,
                      gpointer                  user_data G_GNUC_UNUSED)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *image = gtk_image_new ();
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), label);
  gtk_list_item_set_child (item, box);
}

static void
spot_list_name_bind (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                     GtkListItem              *item,
                     gpointer                  user_data G_GNUC_UNUSED)
{
  GFileInfo *info = gtk_list_item_get_item (item);
  GtkWidget *box = gtk_list_item_get_child (item);
  GtkWidget *image = gtk_widget_get_first_child (box);
  GtkWidget *label = gtk_widget_get_last_child (box);
  GIcon *icon = g_file_info_get_icon (info);

  gtk_image_set_from_gicon (GTK_IMAGE (image), icon);
  gtk_label_set_text (GTK_LABEL (label),
                      g_file_info_get_display_name (info));
}

static void
spot_list_text_setup (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                      GtkListItem              *item,
                      gpointer                  user_data G_GNUC_UNUSED)
{
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_add_css_class (label, "spot-list-dim");
  gtk_list_item_set_child (item, label);
}

static void
spot_list_size_bind (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                     GtkListItem              *item,
                     gpointer                  user_data G_GNUC_UNUSED)
{
  GFileInfo *info = gtk_list_item_get_item (item);
  GtkWidget *label = gtk_list_item_get_child (item);

  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    gtk_label_set_text (GTK_LABEL (label), "—");
  else
    {
      g_autofree char *text = g_format_size (g_file_info_get_size (info));

      gtk_label_set_text (GTK_LABEL (label), text);
    }
}

static void
spot_list_modified_bind (GtkSignalListItemFactory *factory G_GNUC_UNUSED,
                         GtkListItem              *item,
                         gpointer                  user_data G_GNUC_UNUSED)
{
  GFileInfo *info = gtk_list_item_get_item (item);
  GtkWidget *label = gtk_list_item_get_child (item);
  g_autoptr (GDateTime) mtime =
    g_file_info_get_modification_date_time (info);

  if (mtime)
    {
      g_autofree char *text = g_date_time_format (mtime, "%b %-e, %Y %H:%M");

      gtk_label_set_text (GTK_LABEL (label), text);
    }
  else
    gtk_label_set_text (GTK_LABEL (label), "—");
}

static int
spot_list_sort_name (gconstpointer a,
                     gconstpointer b,
                     gpointer      user_data G_GNUC_UNUSED)
{
  return spot_compare_file_info (a, b);
}

static int
spot_list_sort_size (gconstpointer a,
                     gconstpointer b,
                     gpointer      user_data G_GNUC_UNUSED)
{
  goffset sa = g_file_info_get_size ((GFileInfo *) a);
  goffset sb = g_file_info_get_size ((GFileInfo *) b);

  return (sa > sb) - (sa < sb);
}

static int
spot_list_sort_modified (gconstpointer a,
                         gconstpointer b,
                         gpointer      user_data G_GNUC_UNUSED)
{
  g_autoptr (GDateTime) ta =
    g_file_info_get_modification_date_time ((GFileInfo *) a);
  g_autoptr (GDateTime) tb =
    g_file_info_get_modification_date_time ((GFileInfo *) b);

  if (!ta || !tb)
    return (ta != NULL) - (tb != NULL);

  return g_date_time_compare (ta, tb);
}

static void
spot_list_row_activated (GtkColumnView *view G_GNUC_UNUSED,
                         guint          position,
                         gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GtkSelectionModel *model =
    gtk_column_view_get_model (GTK_COLUMN_VIEW (self->list_view));
  g_autoptr (GFileInfo) info =
    g_list_model_get_item (G_LIST_MODEL (model), position);
  GFile *file;

  if (!info)
    return;

  file = g_object_get_data (G_OBJECT (info), "spot-file");
  if (!file)
    return;

  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}

void
spot_populate_list (SpotWindow *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GFileInfo *info;

  g_list_store_remove_all (self->list_store);

  if (!self->current_dir)
    return;

  enumerator = g_file_enumerate_children (self->current_dir,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
                                          G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                                          G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL, &error);
  if (!enumerator)
    return;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      if (g_file_info_get_is_hidden (info))
        {
          g_object_unref (info);
          continue;
        }

      {
        GFile *child = g_file_get_child (self->current_dir,
                                         g_file_info_get_name (info));

        g_object_set_data_full (G_OBJECT (info), "spot-file",
                                child, g_object_unref);
      }
      g_list_store_append (self->list_store, info);
      g_object_unref (info);
    }
}

GtkWidget *
spot_create_list_view (SpotWindow *self)
{
  GtkWidget *scrolled;
  GtkListItemFactory *factory;
  GtkColumnViewColumn *column;
  GtkSorter *sorter;
  GtkSortListModel *sort_model;
  GtkSingleSelection *selection;

  self->list_store = g_list_store_new (G_TYPE_FILE_INFO);

  self->list_view = gtk_column_view_new (NULL);
  gtk_column_view_set_show_row_separators (
    GTK_COLUMN_VIEW (self->list_view), FALSE);
  gtk_widget_add_css_class (self->list_view, "spot-list-view");
  gtk_widget_set_vexpand (self->list_view, TRUE);

  /* Name */
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (spot_list_name_setup), self);
  g_signal_connect (factory, "bind", G_CALLBACK (spot_list_name_bind), self);
  column = gtk_column_view_column_new ("Name", factory);
  gtk_column_view_column_set_expand (column, TRUE);
  gtk_column_view_column_set_resizable (column, TRUE);
  sorter = GTK_SORTER (gtk_custom_sorter_new (spot_list_sort_name, NULL, NULL));
  gtk_column_view_column_set_sorter (column, sorter);
  g_object_unref (sorter);
  gtk_column_view_append_column (GTK_COLUMN_VIEW (self->list_view), column);
  g_object_unref (column);

  /* Size */
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (spot_list_text_setup), self);
  g_signal_connect (factory, "bind", G_CALLBACK (spot_list_size_bind), self);
  column = gtk_column_view_column_new ("Size", factory);
  gtk_column_view_column_set_fixed_width (column, 96);
  gtk_column_view_column_set_resizable (column, TRUE);
  sorter = GTK_SORTER (gtk_custom_sorter_new (spot_list_sort_size, NULL, NULL));
  gtk_column_view_column_set_sorter (column, sorter);
  g_object_unref (sorter);
  gtk_column_view_append_column (GTK_COLUMN_VIEW (self->list_view), column);
  g_object_unref (column);

  /* Modified */
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (spot_list_text_setup), self);
  g_signal_connect (factory, "bind", G_CALLBACK (spot_list_modified_bind), self);
  column = gtk_column_view_column_new ("Modified", factory);
  gtk_column_view_column_set_fixed_width (column, 170);
  gtk_column_view_column_set_resizable (column, TRUE);
  sorter = GTK_SORTER (gtk_custom_sorter_new (spot_list_sort_modified, NULL, NULL));
  gtk_column_view_column_set_sorter (column, sorter);
  g_object_unref (sorter);
  gtk_column_view_append_column (GTK_COLUMN_VIEW (self->list_view), column);
  g_object_unref (column);

  {
    GtkFilterListModel *filter_model;

    self->list_filter =
      GTK_FILTER (gtk_custom_filter_new (spot_list_filter_func, self, NULL));
    filter_model = gtk_filter_list_model_new (
      G_LIST_MODEL (g_object_ref (self->list_store)),
      g_object_ref (self->list_filter));
    sort_model = gtk_sort_list_model_new (
      G_LIST_MODEL (filter_model),
      g_object_ref (gtk_column_view_get_sorter (GTK_COLUMN_VIEW (self->list_view))));
  }
  selection = gtk_single_selection_new (G_LIST_MODEL (sort_model));
  gtk_single_selection_set_autoselect (selection, FALSE);
  gtk_column_view_set_model (GTK_COLUMN_VIEW (self->list_view),
                             GTK_SELECTION_MODEL (selection));
  g_object_unref (selection);

  gtk_column_view_set_single_click_activate (
    GTK_COLUMN_VIEW (self->list_view), FALSE);
  g_signal_connect (self->list_view, "activate",
                    G_CALLBACK (spot_list_row_activated), self);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_widget_add_css_class (scrolled, "spot-list-scroll");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled),
                                 self->list_view);

  return scrolled;
}

/* ── View-mode switching ────────────────────────────────────────────────── */

void
spot_set_view_mode (SpotWindow *self, SpotViewMode mode)
{
  const char *page;
  int active;

  self->view_mode = mode;

  switch (mode)
    {
    case SPOT_VIEW_LIST:    active = 1; page = "list";    break;
    case SPOT_VIEW_COLUMNS: active = 2; page = "columns"; break;
    case SPOT_VIEW_GRID:
    default:                active = 0; page = "grid";    break;
    }
  if (self->view_segment)
    ooze_segment_group_set_active (self->view_segment, active);

  gtk_stack_set_visible_child_name (
      GTK_STACK (self->content_stack), page);

  if (mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else if (mode == SPOT_VIEW_LIST)
    spot_populate_list (self);
  else
    spot_rebuild_columns (self);
}

void
on_view_segment_changed (GtkWidget  *segment G_GNUC_UNUSED,
                         int         index,
                         SpotWindow *self)
{
  static const SpotViewMode modes[] = {
    SPOT_VIEW_GRID, SPOT_VIEW_LIST, SPOT_VIEW_COLUMNS
  };

  if (index >= 0 && index < (int) G_N_ELEMENTS (modes))
    spot_set_view_mode (self, modes[index]);
}


