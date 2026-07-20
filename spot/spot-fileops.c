#include "spot-priv.h"

static char *
spot_unique_child_name (GFile      *parent,
                        const char *base_name)
{
  g_autofree char *name = NULL;
  g_autofree char *stem = NULL;
  g_autofree char *ext = NULL;
  const char *dot;
  int suffix = 0;

  if (!parent || !base_name || !*base_name)
    return g_strdup ("untitled");

  dot = strrchr (base_name, '.');
  if (dot && dot != base_name)
    {
      stem = g_strndup (base_name, (gsize) (dot - base_name));
      ext = g_strdup (dot);
    }
  else
    {
      stem = g_strdup (base_name);
      ext = g_strdup ("");
    }

  while (TRUE)
    {
      g_autoptr (GFile) child = NULL;

      if (suffix == 0)
        name = g_strdup (base_name);
      else if (suffix == 1)
        name = g_strdup_printf ("%s copy%s", stem, ext);
      else
        name = g_strdup_printf ("%s copy %d%s", stem, suffix, ext);

      child = g_file_get_child (parent, name);
      if (!g_file_query_exists (child, NULL))
        return g_steal_pointer (&name);

      g_clear_pointer (&name, g_free);
      suffix++;
    }
}

static gboolean
spot_copy_file_recursive (GFile   *src,
                          GFile   *dest,
                          GError **error)
{
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  info = g_file_query_info (src,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            error);
  if (!info)
    return FALSE;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    {
      g_autoptr (GFileEnumerator) enumerator = NULL;
      g_autoptr (GError) local_error = NULL;
      GFileInfo *child_info;

      if (!g_file_make_directory (dest, NULL, error))
        return FALSE;

      enumerator = g_file_enumerate_children (src,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL,
                                              error);
      if (!enumerator)
        return FALSE;

      while ((child_info = g_file_enumerator_next_file (enumerator, NULL, &local_error)) != NULL)
        {
          const char *name = g_file_info_get_name (child_info);
          g_autoptr (GFile) child_src = g_file_get_child (src, name);
          g_autoptr (GFile) child_dest = g_file_get_child (dest, name);
          gboolean ok;

          ok = spot_copy_file_recursive (child_src, child_dest, error);
          g_object_unref (child_info);
          if (!ok)
            return FALSE;
        }

      if (local_error)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      return TRUE;
    }

  return g_file_copy (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
}

void
spot_spring_cancel (SpotWindow *self)
{
  if (self->spring_id)
    {
      g_source_remove (self->spring_id);
      self->spring_id = 0;
    }
  g_clear_object (&self->spring_target);
}

static gboolean
spot_spring_fire (gpointer user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  self->spring_id = 0;
  if (self->spring_target)
    spot_navigate_to (self, self->spring_target, TRUE);
  g_clear_object (&self->spring_target);
  return G_SOURCE_REMOVE;
}

static void
spot_spring_arm (SpotWindow *self, GFile *folder)
{
  if (!self || !folder)
    return;
  if (self->spring_target && g_file_equal (self->spring_target, folder))
    return;
  spot_spring_cancel (self);
  self->spring_target = g_object_ref (folder);
  self->spring_id = g_timeout_add (700, spot_spring_fire, self);
}

static gboolean
spot_file_is_dir (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;

  if (!file)
    return FALSE;
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
  return info && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY;
}

static gboolean
spot_path_is_descendant (GFile *path, GFile *ancestor)
{
  g_autofree char *p = NULL;
  g_autofree char *a = NULL;

  if (!path || !ancestor)
    return FALSE;
  p = g_file_get_path (path);
  a = g_file_get_path (ancestor);
  if (!p || !a)
    return FALSE;
  if (g_strcmp0 (p, a) == 0)
    return TRUE;
  {
    g_autofree char *prefix = g_strconcat (a, G_DIR_SEPARATOR_S, NULL);
    return g_str_has_prefix (p, prefix);
  }
}

typedef enum
{
  SPOT_XFER_AUTO = 0,
  SPOT_XFER_MOVE,
  SPOT_XFER_COPY,
} SpotXferMode;

static gboolean
spot_same_filesystem (GFile *a, GFile *b)
{
  g_autoptr (GFileInfo) ia = NULL;
  g_autoptr (GFileInfo) ib = NULL;
  const char *ida, *idb;

  ia = g_file_query_info (a, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  ib = g_file_query_info (b, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
  if (!ia || !ib)
    return FALSE;
  ida = g_file_info_get_attribute_string (ia, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  idb = g_file_info_get_attribute_string (ib, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  return ida && idb && g_strcmp0 (ida, idb) == 0;
}

static SpotXferMode
spot_xfer_mode_from_drop (GdkDrop *drop)
{
  GdkDragAction actions;

  if (!drop)
    return SPOT_XFER_AUTO;

  actions = gdk_drop_get_actions (drop);
  if ((actions & GDK_ACTION_MOVE) && !(actions & GDK_ACTION_COPY))
    return SPOT_XFER_MOVE;
  if ((actions & GDK_ACTION_COPY) && !(actions & GDK_ACTION_MOVE))
    return SPOT_XFER_COPY;
  return SPOT_XFER_AUTO;
}

static void
spot_transfer_one (GFile       *src,
                   GFile       *dest_dir,
                   SpotXferMode mode)
{
  g_autofree char *basename = NULL;
  g_autofree char *dest_name = NULL;
  g_autoptr (GFile) dest = NULL;
  g_autoptr (GFile) src_parent = NULL;
  g_autoptr (GError) error = NULL;
  gboolean move;

  if (!src || !dest_dir)
    return;
  if (spot_path_is_descendant (dest_dir, src))
    return;

  src_parent = g_file_get_parent (src);
  if (src_parent && g_file_equal (src_parent, dest_dir))
    return;

  basename = g_file_get_basename (src);
  dest_name = spot_unique_child_name (dest_dir, basename);
  dest = g_file_get_child (dest_dir, dest_name);

  if (mode == SPOT_XFER_COPY)
    move = FALSE;
  else if (mode == SPOT_XFER_MOVE)
    move = TRUE;
  else
    move = spot_same_filesystem (src, dest_dir);

  if (move)
    {
      if (!g_file_move (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
        {
          g_clear_error (&error);
          if (!spot_copy_file_recursive (src, dest, &error))
            g_warning ("Spot: copy failed: %s", error->message);
        }
    }
  else if (!spot_copy_file_recursive (src, dest, &error))
    {
      g_warning ("Spot: copy failed: %s", error->message);
    }
}

static void
spot_transfer_file_list (SpotWindow  *self,
                         GdkFileList *file_list,
                         GFile       *dest_dir,
                         SpotXferMode mode)
{
  GSList *files;
  GSList *l;

  if (!file_list || !dest_dir)
    return;

  files = gdk_file_list_get_files (file_list);
  for (l = files; l != NULL; l = l->next)
    spot_transfer_one (l->data, dest_dir, mode);

  spot_refresh (self);
}

void
spot_window_receive_paths (SpotWindow         *self,
                           const char * const *paths,
                           gboolean            prefer_move)
{
  SpotXferMode mode = prefer_move ? SPOT_XFER_MOVE : SPOT_XFER_AUTO;
  GFile *dest;
  gsize i;

  if (!self || !paths)
    return;

  dest = self->shell_drag_dest ? self->shell_drag_dest : self->current_dir;
  if (!dest)
    return;

  for (i = 0; paths[i] != NULL; i++)
    {
      g_autoptr (GFile) src = g_file_new_for_path (paths[i]);
      spot_transfer_one (src, dest, mode);
    }
  spot_refresh (self);
}

static void
spot_drop_clear_folder_highlight (SpotWindow *self)
{
  if (self->shell_drag_highlight)
    {
      gtk_widget_remove_css_class (self->shell_drag_highlight, "spot-drop-folder");
      self->shell_drag_highlight = NULL;
    }
}

static void
spot_drop_set_content_active (SpotWindow *self, gboolean active)
{
  GtkWidget *target = NULL;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_scrolled)
    target = self->grid_scrolled;
  else if (self->columns_scrolled)
    target = self->columns_scrolled;
  else if (self->content_stack)
    target = self->content_stack;

  if (!target)
    return;

  if (active)
    gtk_widget_add_css_class (target, "spot-drop-active");
  else
    gtk_widget_remove_css_class (target, "spot-drop-active");
}

static GtkWidget *
spot_widget_with_file (GtkWidget *widget)
{
  for (; widget != NULL; widget = gtk_widget_get_parent (widget))
    {
      if (g_object_get_data (G_OBJECT (widget), "spot-file"))
        return widget;
    }
  return NULL;
}

static void
spot_drop_highlight_folder_widget (SpotWindow *self, GtkWidget *widget)
{
  if (self->shell_drag_highlight == widget)
    return;
  spot_drop_clear_folder_highlight (self);
  if (widget)
    {
      /* Prefer the FlowBoxChild / ListBoxRow chrome for a clean outline. */
      GtkWidget *row = widget;
      while (row &&
             !GTK_IS_FLOW_BOX_CHILD (row) &&
             !GTK_IS_LIST_BOX_ROW (row))
        row = gtk_widget_get_parent (row);
      if (!row)
        row = widget;
      self->shell_drag_highlight = row;
      gtk_widget_add_css_class (row, "spot-drop-folder");
    }
}

void
spot_window_shell_drag_leave (SpotWindow *self)
{
  if (!self)
    return;
  spot_spring_cancel (self);
  spot_drop_clear_folder_highlight (self);
  spot_drop_set_content_active (self, FALSE);
  self->shell_drag_active = FALSE;
  g_clear_object (&self->shell_drag_dest);
}

void
spot_window_shell_drag_motion (SpotWindow *self, double x, double y)
{
  GtkWidget *picked;
  GtkWidget *file_widget;
  GFile *file = NULL;
  gboolean is_dir = FALSE;

  if (!self)
    return;

  self->shell_drag_active = TRUE;
  spot_drop_set_content_active (self, TRUE);

  picked = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT);
  file_widget = spot_widget_with_file (picked);
  if (file_widget)
    {
      file = g_object_get_data (G_OBJECT (file_widget), "spot-file");
      is_dir = file && spot_file_is_dir (file);
    }

  if (is_dir && file)
    {
      spot_drop_highlight_folder_widget (self, file_widget);
      g_set_object (&self->shell_drag_dest, file);
      spot_spring_arm (self, file);
    }
  else
    {
      spot_drop_clear_folder_highlight (self);
      g_set_object (&self->shell_drag_dest, self->current_dir);
      spot_spring_cancel (self);
    }
}

static GdkContentProvider *
spot_drag_prepare (GtkDragSource *source G_GNUC_UNUSED,
                   double         x G_GNUC_UNUSED,
                   double         y G_GNUC_UNUSED,
                   gpointer       user_data)
{
  GtkWidget *widget = user_data;
  GFile *file = g_object_get_data (G_OBJECT (widget), "spot-file");
  GdkFileList *file_list;
  GSList *files = NULL;
  g_autofree char *path = NULL;

  if (!file)
    return NULL;

  path = g_file_get_path (file);
  if (path)
    {
      const char *paths[1] = { path };
      ooze_dnd_bridge_set_paths (paths, 1, TRUE);
    }

  files = g_slist_prepend (files, g_object_ref (file));
  file_list = gdk_file_list_new_from_list (files);
  g_slist_free_full (files, g_object_unref);
  return gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list);
}

static gboolean
spot_bridge_clear_later (gpointer user_data G_GNUC_UNUSED)
{
  ooze_dnd_bridge_clear ();
  return G_SOURCE_REMOVE;
}

static void
spot_drag_end (GtkDragSource *source G_GNUC_UNUSED,
               GdkDrag       *drag G_GNUC_UNUSED,
               gboolean       delete_data,
               gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  g_autofree char *hover = NULL;
  gboolean changed = FALSE;

  /* If a GTK drop target already handled the transfer, bridge is empty. */
  if (!ooze_dnd_bridge_has_pending ())
    {
      ooze_dnd_bridge_clear ();
      if (delete_data)
        spot_refresh (self);
      return;
    }

  hover = ooze_dnd_bridge_get_hover_dir ();
  if (hover)
    {
      ooze_dnd_bridge_drop_into (hover);
      changed = TRUE;
    }
  else
    {
      ooze_dnd_bridge_clear ();
    }

  /* Belt-and-suspenders clear in case drop_into partially failed. */
  g_timeout_add (50, spot_bridge_clear_later, NULL);

  /* Desktop / shell drops move files out from under this view. */
  if (changed || delete_data)
    spot_refresh (self);
}

static gboolean
spot_on_drop (GtkDropTarget *target,
              const GValue  *value,
              double         x G_GNUC_UNUSED,
              double         y G_GNUC_UNUSED,
              gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *dest_dir;
  GdkFileList *list;
  GdkDrop *drop;
  SpotXferMode mode;

  spot_spring_cancel (self);
  ooze_dnd_bridge_clear ();

  dest_dir = g_object_get_data (G_OBJECT (target), "spot-drop-dir");
  if (!dest_dir)
    dest_dir = self->current_dir;
  if (!dest_dir || !G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return FALSE;

  list = g_value_get_boxed (value);
  drop = gtk_drop_target_get_current_drop (target);
  mode = spot_xfer_mode_from_drop (drop);

  spot_transfer_file_list (self, list, dest_dir, mode);
  spot_window_shell_drag_leave (self);
  return TRUE;
}

static GdkDragAction
spot_on_drop_enter (GtkDropTarget *target,
                    double         x G_GNUC_UNUSED,
                    double         y G_GNUC_UNUSED,
                    gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *dest_dir = g_object_get_data (G_OBJECT (target), "spot-drop-dir");
  GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target));

  spot_drop_set_content_active (self, TRUE);
  if (dest_dir && spot_file_is_dir (dest_dir))
    {
      spot_drop_highlight_folder_widget (self, widget);
      g_set_object (&self->shell_drag_dest, dest_dir);
      spot_spring_arm (self, dest_dir);
    }
  else
    {
      spot_drop_clear_folder_highlight (self);
      g_set_object (&self->shell_drag_dest, self->current_dir);
    }
  return GDK_ACTION_COPY | GDK_ACTION_MOVE;
}

static void
spot_on_drop_leave (GtkDropTarget *target G_GNUC_UNUSED,
                    gpointer       user_data)
{
  spot_window_shell_drag_leave (SPOT_WINDOW (user_data));
}

void
spot_attach_file_drag (GtkWidget *widget, SpotWindow *self, GFile *file G_GNUC_UNUSED)
{
  GtkDragSource *drag;

  drag = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drag, "prepare", G_CALLBACK (spot_drag_prepare), widget);
  g_signal_connect (drag, "drag-end", G_CALLBACK (spot_drag_end), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drag));
}

void
spot_attach_folder_drop (GtkWidget   *widget,
                         SpotWindow  *self,
                         GFile       *folder)
{
  GtkDropTarget *drop;

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_object_set_data_full (G_OBJECT (drop), "spot-drop-dir",
                          g_object_ref (folder), g_object_unref);
  g_signal_connect (drop, "drop", G_CALLBACK (spot_on_drop), self);
  g_signal_connect (drop, "enter", G_CALLBACK (spot_on_drop_enter), self);
  g_signal_connect (drop, "leave", G_CALLBACK (spot_on_drop_leave), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drop));
}

void
spot_attach_dir_drop (GtkWidget *widget, SpotWindow *self)
{
  GtkDropTarget *drop;

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drop, "drop", G_CALLBACK (spot_on_drop), self);
  g_signal_connect (drop, "enter", G_CALLBACK (spot_on_drop_enter), self);
  g_signal_connect (drop, "leave", G_CALLBACK (spot_on_drop_leave), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drop));
}

void
spot_clipboard_set_files (SpotWindow *self,
                          GFile      *file,
                          gboolean    cut)
{
  GdkClipboard *clipboard;
  GdkContentProvider *provider;
  GdkFileList *file_list;
  GSList *files = NULL;

  if (!file)
    return;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));
  files = g_slist_prepend (files, g_object_ref (file));
  file_list = gdk_file_list_new_from_list (files);
  g_slist_free_full (files, g_object_unref);

  provider = gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list);
  gdk_clipboard_set_content (clipboard, provider);
  g_object_unref (provider);
  /* GdkFileList is consumed/copied by the provider path; drop our ref. */
  g_boxed_free (GDK_TYPE_FILE_LIST, file_list);

  self->clipboard_cut = cut;
  spot_update_action_states (self);
}

void
spot_paste_files_finish (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  const GValue *value = NULL;
  GdkFileList *file_list;
  GSList *files;
  GSList *l;
  gboolean cut = self->clipboard_cut;

  value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source), result, &error);
  if (!value)
    {
      if (error)
        spot_show_error (self, "Paste Failed", error->message);
      return;
    }

  if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return;

  file_list = g_value_get_boxed (value);
  if (!file_list || !self->current_dir)
    return;

  /* transfer container: free the list, not the GFiles */
  files = gdk_file_list_get_files (file_list);
  for (l = files; l != NULL; l = l->next)
    {
      GFile *src = l->data;
      g_autofree char *basename = NULL;
      g_autofree char *dest_name = NULL;
      g_autoptr (GFile) dest = NULL;
      g_autoptr (GError) copy_error = NULL;

      basename = g_file_get_basename (src);
      dest_name = spot_unique_child_name (self->current_dir, basename);
      dest = g_file_get_child (self->current_dir, dest_name);

      if (cut)
        {
          if (!g_file_move (src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &copy_error))
            {
              spot_show_error (self, "Move Failed", copy_error->message);
              cut = FALSE;
              break;
            }
        }
      else if (!spot_copy_file_recursive (src, dest, &copy_error))
        {
          spot_show_error (self, "Paste Failed",
                           copy_error ? copy_error->message : "Could not paste.");
          break;
        }
    }
  g_slist_free (files);

  /* value is owned by the clipboard — do not free/unset it */

  if (cut)
    {
      self->clipboard_cut = FALSE;
      gdk_clipboard_set_content (gtk_widget_get_clipboard (GTK_WIDGET (self)), NULL);
    }

  spot_refresh (self);
  spot_update_action_states (self);
}


void
on_new_folder_shortcut (GtkEventControllerKey *controller G_GNUC_UNUSED,
                        guint                    keyval,
                        guint                    keycode G_GNUC_UNUSED,
                        GdkModifierType          state,
                        SpotWindow              *self)
{
  if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
      (GDK_CONTROL_MASK | GDK_SHIFT_MASK) &&
      (keyval == GDK_KEY_n || keyval == GDK_KEY_N))
    spot_show_new_folder_popover (self);
}


static char *
spot_unique_folder_name (GFile *parent)
{
  int suffix = 0;

  if (!parent)
    return g_strdup ("untitled folder");

  while (TRUE)
    {
      g_autofree char *name = NULL;
      g_autoptr (GFile) child = NULL;

      if (suffix == 0)
        name = g_strdup ("untitled folder");
      else
        name = g_strdup_printf ("untitled folder %d", suffix);

      child = g_file_get_child (parent, name);
      if (!g_file_query_exists (child, NULL))
        return g_steal_pointer (&name);

      suffix++;
    }
}

void
spot_show_error (SpotWindow *self,
                 const char *heading,
                 const char *body)
{
  AdwDialog *dialog;

  dialog = ADW_DIALOG (adw_alert_dialog_new (heading, body));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "ok", "OK");
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "ok",
                                            ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "ok");
  g_signal_connect (dialog, "response", G_CALLBACK (adw_dialog_close), NULL);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static gboolean
spot_create_folder_named (SpotWindow *self,
                          const char *name)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) new_dir = NULL;

  if (!self->current_dir || !name || name[0] == '\0')
    return FALSE;

  if (strchr (name, '/') || strchr (name, G_DIR_SEPARATOR))
    {
      spot_show_error (self,
                       "Invalid Folder Name",
                       "Folder names cannot contain “/”.");
      return FALSE;
    }

  new_dir = g_file_get_child (self->current_dir, name);
  if (!g_file_make_directory (new_dir, NULL, &error))
    {
      spot_show_error (self, "Could Not Create Folder", error->message);
      return FALSE;
    }

  spot_refresh (self);
  return TRUE;
}

void
spot_on_new_folder_create (GtkButton  *button G_GNUC_UNUSED,
                           SpotWindow *self)
{
  const char *name;

  if (!self->new_folder_entry)
    return;

  name = gtk_editable_get_text (GTK_EDITABLE (self->new_folder_entry));
  if (spot_create_folder_named (self, name) && self->new_folder_popover)
    gtk_popover_popdown (GTK_POPOVER (self->new_folder_popover));
}

static void
spot_on_new_folder_entry_activate (GtkEntry   *entry G_GNUC_UNUSED,
                                   SpotWindow *self)
{
  spot_on_new_folder_create (NULL, self);
}

void
spot_show_new_folder_popover (SpotWindow *self)
{
  g_autofree char *default_name = NULL;

  if (!self->current_dir || !self->toolbar)
    return;

  if (!self->new_folder_popover)
    {
      GtkWidget *box;
      GtkWidget *label;
      GtkWidget *create_button;

      self->new_folder_popover = gtk_popover_new ();
      gtk_widget_set_parent (self->new_folder_popover, self->toolbar);

      box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
      gtk_widget_set_margin_top (box, 8);
      gtk_widget_set_margin_bottom (box, 8);
      gtk_widget_set_margin_start (box, 12);
      gtk_widget_set_margin_end (box, 12);

      label = gtk_label_new ("Folder name:");
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_box_append (GTK_BOX (box), label);

      self->new_folder_entry = gtk_entry_new ();
      gtk_widget_set_size_request (self->new_folder_entry, 220, -1);
      gtk_box_append (GTK_BOX (box), self->new_folder_entry);
      g_signal_connect (self->new_folder_entry,
                        "activate",
                        G_CALLBACK (spot_on_new_folder_entry_activate),
                        self);

      create_button = gtk_button_new_with_label ("Create");
      gtk_widget_set_halign (create_button, GTK_ALIGN_END);
      g_signal_connect (create_button, "clicked", G_CALLBACK (spot_on_new_folder_create), self);
      gtk_box_append (GTK_BOX (box), create_button);

      gtk_popover_set_child (GTK_POPOVER (self->new_folder_popover), box);
    }

  default_name = spot_unique_folder_name (self->current_dir);
  gtk_editable_set_text (GTK_EDITABLE (self->new_folder_entry), default_name);
  gtk_popover_popup (GTK_POPOVER (self->new_folder_popover));
  gtk_entry_grab_focus_without_selecting (GTK_ENTRY (self->new_folder_entry));
  gtk_editable_select_region (GTK_EDITABLE (self->new_folder_entry), 0, -1);
}


