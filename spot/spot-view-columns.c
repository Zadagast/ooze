#include "spot-priv.h"

static void
spot_scroll_columns_to_end (SpotWindow *self)
{
  GtkAdjustment *adj;

  if (!self->columns_scrolled)
    return;

  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->columns_scrolled));
  if (!adj)
    return;

  gtk_adjustment_set_value (adj,
                            gtk_adjustment_get_upper (adj) -
                            gtk_adjustment_get_page_size (adj));
}


static GFile *
spot_column_browser_root (GFile *dir)
{
  g_autoptr (GFile) best = NULL;
  gsize i;

  if (!dir)
    return NULL;

  /* Home and special dirs first — longest / deepest match wins. */
  for (i = 0; i < spot_sidebar_places_len; i++)
    {
      g_autofree char *path = NULL;
      g_autoptr (GFile) place = NULL;

      path = spot_place_path_dup (&spot_sidebar_places[i]);
      if (!path)
        continue;

      place = g_file_new_for_path (path);
      if (!g_file_equal (dir, place) && !g_file_has_prefix (dir, place))
        continue;

      if (!best || g_file_has_prefix (place, best) || g_file_equal (place, best))
        g_set_object (&best, place);
    }

  /* Fallback: filesystem root ("Linux HD") */
  if (!best)
    best = g_file_new_for_path ("/");

  return g_steal_pointer (&best);
}

/* Path chain from browser_root down to dir (inclusive). */
static GList *
spot_path_chain_from_root (GFile *dir,
                           GFile *root)
{
  GList *chain = NULL;
  GFile *current;

  if (!dir || !root)
    return NULL;

  current = g_object_ref (dir);
  while (current)
    {
      gboolean at_root = g_file_equal (current, root);

      chain = g_list_prepend (chain, current);
      if (at_root)
        break;

      {
        g_autoptr (GFile) parent = g_file_get_parent (current);

        current = parent ? g_object_ref (parent) : NULL;
      }
    }

  return chain;
}


static GtkWidget *
spot_create_column_list (SpotWindow *self,
                         GFile      *directory,
                         GFile      *select_child)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GtkWidget *scrolled;
  GtkWidget *list;
  GtkWidget *selected_row = NULL;

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled, self->column_width, -1);
  gtk_widget_set_hexpand (scrolled, FALSE);
  gtk_widget_set_vexpand (scrolled, TRUE);
  gtk_widget_add_css_class (scrolled, "spot-column");
  /* Flush gutter: only the candy thumb shows, so columns sit snug. */
  gtk_widget_add_css_class (scrolled, "ooze-scroll-flush");
  /* Overlay: the thumb floats on the column's right edge instead of
   * reserving a lane, so the next column kisses this one. */
  gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scrolled), TRUE);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  /* Miller columns: single-click drills into the next column. */
  gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (list), TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);

  enumerator = g_file_enumerate_children (directory,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);
  if (!enumerator)
    {
      GtkWidget *row = gtk_list_box_row_new ();
      GtkWidget *label = gtk_label_new (error ? error->message : "Unable to read folder");
      gtk_widget_set_margin_start (label, 6);
      gtk_widget_set_margin_end (label, 6);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
      return scrolled;
    }

  GList *entries = NULL;
  GList *l;
  GFileInfo *info;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      if (g_file_info_get_is_hidden (info))
        {
          g_object_unref (info);
          continue;
        }

      entries = g_list_prepend (entries, info);
    }

  entries = g_list_sort (entries, spot_compare_file_info);

  for (l = entries; l != NULL; l = l->next)
    {
      GFileInfo *entry = l->data;
      GtkWidget *row;
      GtkWidget *box;
      GtkWidget *image;
      GtkWidget *label;
      g_autoptr (GFile) child = NULL;
      const char *display_name;

      child = g_file_get_child (directory, g_file_info_get_name (entry));
      row = gtk_list_box_row_new ();
      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_widget_set_margin_top (box, 2);
      gtk_widget_set_margin_bottom (box, 2);

      image = spot_image_new_for_info (entry);

      display_name = g_file_info_get_display_name (entry);
      label = gtk_label_new (display_name);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

      gtk_box_append (GTK_BOX (box), image);
      gtk_box_append (GTK_BOX (box), label);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

      g_object_set_data_full (G_OBJECT (row), "spot-file", g_object_ref (child), g_object_unref);
      spot_attach_file_drag (row, self, child);
      if (g_file_info_get_file_type (entry) == G_FILE_TYPE_DIRECTORY)
        spot_attach_folder_drop (row, self, child);
      gtk_list_box_append (GTK_LIST_BOX (list), row);

      if (select_child && g_file_equal (child, select_child))
        selected_row = row;
    }

  if (!entries)
    {
      GtkWidget *row = gtk_list_box_row_new ();
      GtkWidget *label = gtk_label_new ("Empty");

      gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
      gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
      gtk_widget_add_css_class (label, "spot-empty-hint");
      gtk_widget_set_margin_top (label, 12);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
    }

  g_list_free_full (entries, g_object_unref);

  if (selected_row)
    gtk_list_box_select_row (GTK_LIST_BOX (list), GTK_LIST_BOX_ROW (selected_row));

  g_signal_connect (list, "row-activated", G_CALLBACK (spot_on_column_row_activated), self);
  spot_attach_context_menu (self, list);

  return scrolled;
}

/* Pointer x in columns_box space — the handle itself moves while the
 * column beside it resizes, so its own coordinate frame is unstable. */
static gboolean
spot_column_handle_pointer_x (GtkGestureDrag *gesture,
                              GtkWidget      *handle,
                              SpotWindow     *self,
                              double         *out_x)
{
  GdkEventSequence *seq;
  graphene_point_t local;
  graphene_point_t in_box;
  double x, y;

  seq = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (gesture));
  if (!gtk_gesture_get_point (GTK_GESTURE (gesture), seq, &x, &y))
    return FALSE;

  local = GRAPHENE_POINT_INIT ((float) x, (float) y);
  if (!gtk_widget_compute_point (handle, self->columns_box, &local, &in_box))
    return FALSE;

  *out_x = in_box.x;
  return TRUE;
}

static void
spot_column_handle_drag_begin (GtkGestureDrag *gesture,
                               double          x G_GNUC_UNUSED,
                               double          y G_GNUC_UNUSED,
                               SpotWindow     *self)
{
  GtkWidget *handle;
  GtkWidget *left;
  double box_x;

  handle = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  left = gtk_widget_get_prev_sibling (handle);
  if (!left || !gtk_widget_has_css_class (left, "spot-column") ||
      !spot_column_handle_pointer_x (gesture, handle, self, &box_x))
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  g_object_set_data (G_OBJECT (gesture), "spot-left-column", left);
  g_object_set_data (G_OBJECT (gesture), "spot-start-width",
                     GINT_TO_POINTER (gtk_widget_get_width (left)));
  g_object_set_data (G_OBJECT (gesture), "spot-start-x",
                     GINT_TO_POINTER ((int) box_x));
}

static void
spot_column_handle_drag_update (GtkGestureDrag *gesture,
                                double          offset_x G_GNUC_UNUSED,
                                double          offset_y G_GNUC_UNUSED,
                                SpotWindow     *self)
{
  GtkWidget *handle;
  GtkWidget *left;
  int start_width;
  int start_x;
  int width;
  double box_x;

  handle = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  left = g_object_get_data (G_OBJECT (gesture), "spot-left-column");
  if (!left || !spot_column_handle_pointer_x (gesture, handle, self, &box_x))
    return;

  start_width = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (gesture),
                                                    "spot-start-width"));
  start_x = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (gesture),
                                                "spot-start-x"));

  width = CLAMP (start_width + ((int) box_x - start_x),
                 SPOT_COLUMN_WIDTH_MIN,
                 SPOT_COLUMN_WIDTH_MAX);

  gtk_widget_set_size_request (left, width, -1);
  /* New columns (and the capacity math) adopt the width you set. */
  self->column_width = width;
}

/* Keyboard navigation for Miller columns: Left steps out to the parent,
 * Right drills into the selected folder (Finder style). Up/Down within a
 * column are native GtkListBox behaviour. */
static GtkListBox *
spot_columns_last_list (SpotWindow *self)
{
  GtkWidget *child = gtk_widget_get_last_child (self->columns_box);
  GtkWidget *inner;

  if (!child || !GTK_IS_SCROLLED_WINDOW (child))
    return NULL;

  inner = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (child));
  if (GTK_IS_VIEWPORT (inner))
    inner = gtk_viewport_get_child (GTK_VIEWPORT (inner));

  return GTK_IS_LIST_BOX (inner) ? GTK_LIST_BOX (inner) : NULL;
}

gboolean
spot_columns_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                          guint                  keyval,
                          guint                  keycode G_GNUC_UNUSED,
                          GdkModifierType        state,
                          gpointer               user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  if (self->view_mode != SPOT_VIEW_COLUMNS)
    return FALSE;
  if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) != 0)
    return FALSE;

  if (keyval == GDK_KEY_Left)
    {
      g_autoptr (GFile) parent =
        self->current_dir ? g_file_get_parent (self->current_dir) : NULL;

      if (!parent)
        return FALSE;

      spot_navigate_to (self, parent, TRUE);
      return TRUE;
    }

  if (keyval == GDK_KEY_Right)
    {
      GtkListBox *list = spot_columns_last_list (self);
      GtkListBoxRow *row;
      GFile *file;

      if (!list)
        return FALSE;

      row = gtk_list_box_get_selected_row (list);
      if (!row)
        {
          row = gtk_list_box_get_row_at_index (list, 0);
          if (row)
            {
              gtk_list_box_select_row (list, row);
              gtk_widget_grab_focus (GTK_WIDGET (row));
            }
          return row != NULL;
        }

      file = g_object_get_data (G_OBJECT (row), "spot-file");
      if (!file)
        return FALSE;

      {
        g_autoptr (GFileInfo) info = g_file_query_info (
          file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
          G_FILE_QUERY_INFO_NONE, NULL, NULL);

        if (info &&
            g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
          {
            spot_navigate_to (self, file, TRUE);
            return TRUE;
          }
      }

      return FALSE;
    }

  return FALSE;
}

/* Column width persists across sessions in the Ooze config dir. */
static char *
spot_state_path_dup (void)
{
  return g_build_filename (g_get_user_config_dir (), "ooze",
                           "spot-state.ini", NULL);
}

void
spot_state_load (SpotWindow *self)
{
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autofree char *path = spot_state_path_dup ();
  int width;

  if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
    return;

  width = g_key_file_get_integer (keyfile, "columns", "width", NULL);
  if (width >= SPOT_COLUMN_WIDTH_MIN && width <= SPOT_COLUMN_WIDTH_MAX)
    self->column_width = width;
}

static void
spot_state_save (SpotWindow *self)
{
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autofree char *path = spot_state_path_dup ();
  g_autofree char *dir = g_path_get_dirname (path);

  g_key_file_load_from_file (keyfile, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
  g_key_file_set_integer (keyfile, "columns", "width", self->column_width);
  g_mkdir_with_parents (dir, 0700);
  g_key_file_save_to_file (keyfile, path, NULL);
}

static void
spot_column_handle_drag_end (GtkGestureDrag *gesture G_GNUC_UNUSED,
                             double          offset_x G_GNUC_UNUSED,
                             double          offset_y G_GNUC_UNUSED,
                             SpotWindow     *self)
{
  spot_state_save (self);
}

/* Divider between Miller columns: an Aqua pinline widened into a
 * grab strip that drag-resizes the column to its left, Finder style. */
static GtkWidget *
spot_create_column_handle (SpotWindow *self)
{
  GtkWidget *handle;
  GtkWidget *pin;
  GtkGesture *drag;

  handle = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (handle, "spot-column-handle");
  gtk_widget_set_size_request (handle, 5, -1);
  gtk_widget_set_cursor_from_name (handle, "col-resize");

  pin = ooze_pinline_new (OOZE_SIDE_RIGHT);
  gtk_widget_set_hexpand (pin, FALSE);
  gtk_widget_set_halign (pin, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (pin, 2);
  gtk_widget_set_hexpand (handle, FALSE);
  gtk_box_append (GTK_BOX (handle), pin);

  drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin",
                    G_CALLBACK (spot_column_handle_drag_begin), self);
  g_signal_connect (drag, "drag-update",
                    G_CALLBACK (spot_column_handle_drag_update), self);
  g_signal_connect (drag, "drag-end",
                    G_CALLBACK (spot_column_handle_drag_end), self);
  gtk_widget_add_controller (handle, GTK_EVENT_CONTROLLER (drag));

  return handle;
}

static void
spot_clear_columns (SpotWindow *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->columns_box)) != NULL)
    gtk_box_remove (GTK_BOX (self->columns_box), child);
}

void
spot_rebuild_columns (SpotWindow *self)
{
  g_autoptr (GFile) root = NULL;
  g_autolist (GFile) chain = NULL;
  GList *l;
  gboolean first = TRUE;

  spot_clear_columns (self);

  if (!self->current_dir)
    return;

  /* Finder model: the FULL chain from root is always present; the pane
   * scrolls horizontally and stays pinned at the current folder. */
  root = spot_column_browser_root (self->current_dir);
  chain = spot_path_chain_from_root (self->current_dir, root);

  for (l = chain; l != NULL; l = l->next)
    {
      GFile *dir = l->data;
      GFile *select = l->next ? l->next->data : NULL;
      GtkWidget *column;

      if (!first)
        gtk_box_append (GTK_BOX (self->columns_box),
                        spot_create_column_handle (self));
      first = FALSE;

      column = spot_create_column_list (self, dir, select);
      gtk_box_append (GTK_BOX (self->columns_box), column);
    }

  spot_scroll_columns_to_end (self);
}


void
spot_on_column_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                              GtkListBoxRow *row,
                              gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *file = g_object_get_data (G_OBJECT (row), "spot-file");
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  if (!file)
    return;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info)
    return;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    spot_navigate_to (self, file, TRUE);
  else
    spot_open_file (file);
}


