#include "spot-priv.h"

G_DEFINE_FINAL_TYPE (SpotWindow, spot_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void
spot_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GtkCssProvider *provider;
  GdkDisplay *display;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider,
                                     /*
                                      * ── Native CSD decoration (Aqua shadow + corner radius) ──
                                      *
                                      * GTK4 wraps every CSD window in a "decoration" node.
                                      * We override it to get the classic Aqua drop-shadow and
                                      * tight 9 px corner radius.
                                      */
                                     "window.csd.spot-finder > decoration {"
                                     "  border-radius: 9px;"
                                     "  box-shadow:"
                                     "    0 2px  6px rgba(0,0,0,0.22),"
                                     "    0 8px 24px rgba(0,0,0,0.40),"
                                     "    0 20px 40px rgba(0,0,0,0.20);"
                                     "}"
                                     "window.csd.spot-finder > decoration:focus {"
                                     "  box-shadow:"
                                     "    0 2px  6px rgba(0,0,0,0.28),"
                                     "    0 10px 30px rgba(0,0,0,0.48),"
                                     "    0 22px 44px rgba(0,0,0,0.22);"
                                     "}"
                                     /* Window body uses Adwaita's adaptive token — white in
                                      * light mode, dark in dark mode, no CSS override needed. */
                                     "window.csd.spot-finder { background: @window_bg_color; }"

                                     /*
                                      * ── Layout / typography only ─────────────────────────────
                                      *
                                      * Backgrounds for toolbar / sidebar / statusbar are drawn by
                                      * OozeSurface (ooze-kit).  Button chrome is drawn by
                                      * OozeButton (ooze-kit).  CSS here is sizing + text only.
                                      */

                                     /* Window content area — adaptive */
                                     ".spot-finder { background: @window_bg_color; }"

                                     /*
                                      * Flush chrome → content join.
                                      * Adwaita/GTK defaults add radius/shadow that make the
                                      * view look like a floating card under the toolbar.
                                      * Square edges so toolbar/sidebar pinlines meet cleanly.
                                      */
                                     ".spot-finder paned,"
                                     ".spot-finder paned > *,"
                                     ".spot-finder stack,"
                                     ".spot-finder scrolledwindow:not(.spot-column),"
                                     ".spot-finder scrolledwindow > viewport,"
                                     ".spot-finder listbox,"
                                     ".spot-finder .spot-grid-scroll,"
                                     ".spot-finder .spot-grid-view,"
                                     ".spot-finder .ooze-surface {"
                                     "  border-radius: 0;"
                                     "  box-shadow: none;"
                                     "  border: none;"
                                     "  margin: 0;"
                                     "  outline: none;"
                                     "}"

                                     /* Column dividers are OozePinline siblings (not CSS shadows). */

                                     /* Search shape only — strip sizing comes from OozeToolbar. */
                                     ".ooze-toolbar .spot-search {"
                                     "  min-width: 120px;"
                                     "  border-radius: 10px;"
                                     "}"

                                     /* ── Sidebar ── */
                                     ".spot-sidebar-list { background: none; }"
                                     ".spot-sidebar-list row.spot-sidebar-heading,"
                                     ".spot-sidebar-list row.spot-sidebar-heading:hover {"
                                     "  background: none;"
                                     "  padding: 7px 8px 1px 8px;"
                                     "}"
                                     ".spot-sidebar-heading label {"
                                     "  font-size: 0.72em;"
                                     "  font-weight: 700;"
                                     "  color: alpha(@sidebar_fg_color, 0.55);"
                                     "}"
                                     ".spot-sidebar-list row { padding: 3px 8px; }"
                                     ".spot-sidebar-list row:hover { background: rgba(128,128,128,0.10); }"
                                     ".spot-sidebar-list row:selected {"
                                     "  background: @accent_bg_color;"
                                     "}"
                                     ".spot-sidebar-list .spot-sidebar-label {"
                                     "  color: @sidebar_fg_color;"
                                     "}"
                                     ".spot-sidebar-list row:selected .spot-sidebar-label {"
                                     "  color: @accent_fg_color;"
                                     "}"

                                     /* ── Column browser ──
                                      * Pane fill only; Aqua pinlines are ooze_pinline_new()
                                      * siblings between columns (box-shadow is clipped). */
                                     ".spot-finder .spot-column {"
                                     "  min-width: 120px;"
                                     "  border-radius: 0;"
                                     "  margin: 0;"
                                     "  border: none;"
                                     "  background: @view_bg_color;"
                                     "  box-shadow: none;"
                                     "}"
                                     ".spot-finder .spot-column > viewport,"
                                     ".spot-finder .spot-column listbox {"
                                     "  background: transparent;"
                                     "  border: none;"
                                     "  box-shadow: none;"
                                     "}"
                                     ".spot-column row { padding: 2px 8px;"
                                     "                   color: @view_fg_color; }"
                                     ".spot-column row:hover { background: alpha(@accent_bg_color, 0.10); }"
                                     ".spot-column row:selected {"
                                     "  background: @accent_bg_color;"
                                     "  color: @accent_fg_color;"
                                     "}"
                                     /* Grab strip between columns (hosts the pinline). */
                                     ".spot-column-handle { background: none; }"
                                     ".spot-column-handle:hover {"
                                     "  background: alpha(@accent_bg_color, 0.20);"
                                     "}"

                                     /* ── Path bar (Finder style) ── */
                                     ".spot-pathbar { background: none; }"
                                     ".spot-pathbar button {"
                                     "  background: none;"
                                     "  border: none;"
                                     "  box-shadow: none;"
                                     "  outline: none;"
                                     "  padding: 0px 4px;"
                                     "  min-height: 0;"
                                     "  color: @window_fg_color;"
                                     "}"
                                     ".spot-pathbar button:hover {"
                                     "  background: alpha(@accent_bg_color, 0.15);"
                                     "  border-radius: 4px;"
                                     "}"
                                     ".spot-pathbar button label { font-size: 0.92em; }"
                                     ".spot-pathbar .spot-crumb-sep {"
                                     "  color: alpha(@window_fg_color, 0.40);"
                                     "  font-size: 0.92em;"
                                     "}"

                                     /* ── Status bar ──
                                      * Surface is edge-flush; OozeKit insets only the label
                                      * (.ooze-surface-statusbar > *) for CSD corner clearance. */
                                     ".spot-status-count {"
                                     "  color: alpha(@window_fg_color, 0.65);"
                                     "}"

                                     /* Content views share one flat plane under the toolbar. */
                                     ".spot-grid-scroll,"
                                     ".spot-grid-scroll > viewport,"
                                     ".spot-grid-view {"
                                     "  background: @view_bg_color;"
                                     "}"

                                     /* Selection + hover chrome on the FlowBoxChild */
                                     ".spot-grid-view > flowboxchild {"
                                     "  border-radius: 5px;"
                                     "  outline: none;"
                                     "  background: none;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected {"
                                     "  background: @accent_bg_color;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:hover:not(:selected) {"
                                     "  background: alpha(@accent_bg_color, 0.10);"
                                     "}"

                                     /* Muted hints and secondary list columns */
                                     ".spot-empty-hint, .spot-list-dim {"
                                     "  color: alpha(@window_fg_color, 0.55);"
                                     "}"

                                     /* Filename label — colour follows window fg, not view fg */
                                     ".spot-grid-label {"
                                     "  color: @window_fg_color;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected .spot-grid-label {"
                                     "  color: @accent_fg_color;"
                                     "}"

                                     /* Shell / GTK drop feedback */
                                     ".spot-finder .spot-drop-active {"
                                     "  outline: 2px solid @accent_bg_color;"
                                     "  outline-offset: -2px;"
                                     "  background: alpha(@accent_bg_color, 0.08);"
                                     "}"
                                     ".spot-finder .spot-drop-folder {"
                                     "  outline: 2px solid @accent_bg_color;"
                                     "  outline-offset: -2px;"
                                     "  background: alpha(@accent_bg_color, 0.18);"
                                     "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  loaded = TRUE;
}


void
spot_set_context_target (SpotWindow *self,
                         GFile      *file)
{
  if (self->context_target == file)
    return;
  g_clear_object (&self->context_target);
  if (file)
    self->context_target = g_object_ref (file);
}

static GFile *
spot_find_file_on_widget (GtkWidget *widget)
{
  while (widget)
    {
      GFile *file = g_object_get_data (G_OBJECT (widget), "spot-file");
      if (file)
        return file;
      widget = gtk_widget_get_parent (widget);
    }
  return NULL;
}

GFile *
spot_pick_file_at (GtkWidget *widget,
                   double     x,
                   double     y)
{
  GtkWidget *picked;

  picked = gtk_widget_pick (widget, x, y, GTK_PICK_DEFAULT);
  return spot_find_file_on_widget (picked);
}

GFile *
spot_get_selected_file (SpotWindow *self)
{
  if (self->context_target)
    return self->context_target;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_flow)
    {
      g_autoptr (GList) selected = NULL;
      GtkFlowBoxChild *child;
      GtkWidget *cell;

      selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (self->grid_flow));
      if (!selected)
        return NULL;
      child = selected->data;
      cell = gtk_flow_box_child_get_child (child);
      return spot_find_file_on_widget (cell);
    }

  if (self->columns_box)
    {
      GtkWidget *scrolled;

      for (scrolled = gtk_widget_get_last_child (self->columns_box);
           scrolled != NULL;
           scrolled = gtk_widget_get_prev_sibling (scrolled))
        {
          GtkWidget *list;
          GtkListBoxRow *row;

          if (!GTK_IS_SCROLLED_WINDOW (scrolled))
            continue;
          list = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scrolled));
          if (!GTK_IS_LIST_BOX (list))
            continue;
          row = gtk_list_box_get_selected_row (GTK_LIST_BOX (list));
          if (row)
            return spot_find_file_on_widget (GTK_WIDGET (row));
        }
    }

  return NULL;
}

void
spot_select_file_widget (SpotWindow *self,
                         GtkWidget  *host G_GNUC_UNUSED,
                         GFile      *file)
{
  if (!file)
    return;

  if (self->view_mode == SPOT_VIEW_GRID && self->grid_flow)
    {
      GtkWidget *child;

      for (child = gtk_widget_get_first_child (self->grid_flow);
           child != NULL;
           child = gtk_widget_get_next_sibling (child))
        {
          GtkWidget *cell;
          GFile *f;

          if (!GTK_IS_FLOW_BOX_CHILD (child))
            continue;
          cell = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
          f = spot_find_file_on_widget (cell);
          if (f && g_file_equal (f, file))
            {
              gtk_flow_box_select_child (GTK_FLOW_BOX (self->grid_flow),
                                         GTK_FLOW_BOX_CHILD (child));
              return;
            }
        }
    }
  else if (self->columns_box)
    {
      GtkWidget *scrolled;

      for (scrolled = gtk_widget_get_first_child (self->columns_box);
           scrolled != NULL;
           scrolled = gtk_widget_get_next_sibling (scrolled))
        {
          GtkWidget *list;
          GtkWidget *row;

          if (!GTK_IS_SCROLLED_WINDOW (scrolled))
            continue;
          list = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scrolled));
          if (!GTK_IS_LIST_BOX (list))
            continue;

          for (row = gtk_widget_get_first_child (list);
               row != NULL;
               row = gtk_widget_get_next_sibling (row))
            {
              GFile *f;

              if (!GTK_IS_LIST_BOX_ROW (row))
                continue;
              f = spot_find_file_on_widget (row);
              if (f && g_file_equal (f, file))
                {
                  gtk_list_box_select_row (GTK_LIST_BOX (list),
                                           GTK_LIST_BOX_ROW (row));
                  return;
                }
            }
        }
    }
}

GtkWidget *
spot_image_new_from_icon_list (const char * const *icon_names,
                               int                   size,
                               gboolean              prefer_color G_GNUC_UNUSED)
{
  return ooze_icon_image_new (icon_names, size);
}

static const char * const spot_icon_back[] = {
  "go-previous", "go-previous-symbolic", NULL
};
static const char * const spot_icon_forward[] = {
  "go-next", "go-next-symbolic", NULL
};
static const char * const spot_icon_view_grid[] = {
  "view-grid", "view-app-grid", "view-grid-symbolic", NULL
};
static const char * const spot_icon_view_list[] = {
  "view-list", "view-list-symbolic", "view-continuous-symbolic", NULL
};
static const char * const spot_icon_view_column[] = {
  "view-column", "view-dual", "view-column-symbolic", "view-dual-symbolic", NULL
};
static const char * const spot_icon_computer[] = {
  "computer", "computer-symbolic", "drive-harddisk", NULL
};

static GtkWidget *
spot_create_toolbar_button (const char * const *icon_names,
                            const char         *label,
                            const char         *tooltip,
                            gboolean            toggle,
                            gboolean            active)
{
  GtkWidget *button;
  GtkWidget *child;

  button = ooze_button_new_toolbar (icon_names, label, tooltip);
  if (toggle && active)
    ooze_button_set_toggled (button, TRUE);

  /* Cap label width so the toolbar cannot push window min past half-screen. */
  child = gtk_button_get_child (GTK_BUTTON (button));
  if (GTK_IS_BOX (child))
    {
      GtkWidget *w = gtk_widget_get_first_child (child);
      while (w)
        {
          if (GTK_IS_LABEL (w))
            {
              gtk_label_set_ellipsize (GTK_LABEL (w), PANGO_ELLIPSIZE_END);
              gtk_label_set_max_width_chars (GTK_LABEL (w), 11);
            }
          w = gtk_widget_get_next_sibling (w);
        }
    }

  return button;
}

int
spot_compare_file_info (gconstpointer a,
                        gconstpointer b)
{
  GFileInfo *ia = (GFileInfo *) a;
  GFileInfo *ib = (GFileInfo *) b;
  gboolean dir_a;
  gboolean dir_b;
  const char *name_a;
  const char *name_b;

  dir_a = g_file_info_get_file_type (ia) == G_FILE_TYPE_DIRECTORY;
  dir_b = g_file_info_get_file_type (ib) == G_FILE_TYPE_DIRECTORY;
  if (dir_a != dir_b)
    return dir_b - dir_a;

  name_a = g_file_info_get_display_name (ia);
  name_b = g_file_info_get_display_name (ib);
  return g_ascii_strcasecmp (name_a, name_b);
}

static void
spot_update_nav_buttons (SpotWindow *self)
{
  gtk_widget_set_sensitive (self->back_button, self->back_stack != NULL);
  gtk_widget_set_sensitive (self->forward_button, self->forward_stack != NULL);
}

static void
spot_update_title (SpotWindow *self)
{
  if (!self->current_dir)
    {
      ooze_application_window_set_title (
        OOZE_APPLICATION_WINDOW (self), "Spot");
      return;
    }

  {
    g_autofree char *basename = g_file_get_basename (self->current_dir);
    const char *title = basename;

    if (!title || title[0] == '\0')
      title = "/";

    ooze_application_window_set_title (
      OOZE_APPLICATION_WINDOW (self), title);
  }
}

static void
spot_update_status (SpotWindow *self)
{
  int count = 0;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  if (!self->current_dir)
    {
      gtk_label_set_text (GTK_LABEL (self->status_label), "");
      return;
    }

  enumerator = g_file_enumerate_children (self->current_dir,
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);
  if (enumerator)
    {
      GFileInfo *info;

      while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
        {
          if (!g_file_info_get_is_hidden (info))
            count++;
          g_object_unref (info);
        }
    }

  {
    g_autofree char *text = g_strdup_printf ("%d item%s",
                                             count,
                                             count == 1 ? "" : "s");
    gtk_label_set_text (GTK_LABEL (self->status_label), text);
  }
}

static void
spot_set_current_dir (SpotWindow *self,
                      GFile      *dir)
{
  g_set_object (&self->current_dir, dir);
  spot_update_nav_buttons (self);
  spot_update_title (self);
}

static void
spot_push_history (SpotWindow *self,
                   GFile      *dir)
{
  if (!self->current_dir)
    return;

  self->back_stack = g_list_prepend (self->back_stack,
                                     g_object_ref (self->current_dir));
  g_list_free_full (self->forward_stack, g_object_unref);
  self->forward_stack = NULL;
  spot_set_current_dir (self, dir);
}

void
spot_pop_history (SpotWindow *self,
                  GList     **stack,
                  GList     **other_stack)
{
  GList *link;
  GFile *dir;

  if (!*stack)
    return;

  link = *stack;
  dir = link->data;
  *stack = g_list_remove_link (*stack, link);
  g_list_free_1 (link);

  if (self->current_dir)
    *other_stack = g_list_prepend (*other_stack,
                                   g_object_ref (self->current_dir));

  spot_set_current_dir (self, g_object_ref (dir));
  g_object_unref (dir);
}

static void
spot_launch_uri (const char *uri)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GAppLaunchContext) ctx = g_app_launch_context_new ();
  g_autoptr (GFile) file = NULL;
  g_autoptr (GAppInfo) info = NULL;

  file = g_file_new_for_uri (uri);
  info = g_file_query_default_handler (file, NULL, NULL);
  ooze_appmenu_prepare_launch_context_for_info (ctx, info);

  if (info)
    {
      GList uris = { .data = (gpointer) uri, .next = NULL, .prev = NULL };

      if (!g_app_info_launch_uris (info, &uris, ctx, &error))
        g_warning ("Spot: failed to open %s: %s", uri, error->message);
      return;
    }

  if (!g_app_info_launch_default_for_uri (uri, ctx, &error))
    g_warning ("Spot: failed to open %s: %s", uri, error->message);
}

void
spot_open_file (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;
  GFileType type;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info)
    return;

  type = g_file_info_get_file_type (info);
  if (type == G_FILE_TYPE_DIRECTORY)
    return;

  spot_launch_uri (g_file_get_uri (file));
}

static void
on_pathbar_crumb_clicked (GtkButton *button,
                          gpointer   user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  GFile *target = g_object_get_data (G_OBJECT (button), "spot-file");

  if (target)
    spot_navigate_to (self, target, TRUE);
}

/* Finder-style path bar: one clickable crumb per ancestor of the
 * current folder, root first. */
static void
spot_update_pathbar (SpotWindow *self)
{
  GtkWidget *child;
  GList *chain = NULL;
  GList *l;
  GFile *iter;

  if (!self->pathbar)
    return;

  while ((child = gtk_widget_get_first_child (self->pathbar)) != NULL)
    gtk_box_remove (GTK_BOX (self->pathbar), child);

  if (!self->current_dir)
    return;

  iter = g_object_ref (self->current_dir);
  while (iter)
    {
      chain = g_list_prepend (chain, iter);
      iter = g_file_get_parent (iter);
    }

  for (l = chain; l != NULL; l = l->next)
    {
      GFile *dir = l->data;
      g_autofree char *name = g_file_get_basename (dir);
      const char *label = name;
      GtkWidget *crumb;

      if (!label || g_strcmp0 (label, "/") == 0)
        label = "Linux HD";

      if (l != chain)
        {
          GtkWidget *sep = gtk_label_new ("\342\226\270");
          gtk_widget_add_css_class (sep, "spot-crumb-sep");
          gtk_box_append (GTK_BOX (self->pathbar), sep);
        }

      crumb = gtk_button_new_with_label (label);
      gtk_button_set_has_frame (GTK_BUTTON (crumb), FALSE);
      g_object_set_data_full (G_OBJECT (crumb), "spot-file",
                              g_object_ref (dir), g_object_unref);
      g_signal_connect (crumb, "clicked",
                        G_CALLBACK (on_pathbar_crumb_clicked), self);
      gtk_box_append (GTK_BOX (self->pathbar), crumb);
    }

  g_list_free_full (chain, g_object_unref);
}

void
spot_refresh (SpotWindow *self)
{
  if (self->view_mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else if (self->view_mode == SPOT_VIEW_LIST)
    spot_populate_list (self);
  else
    spot_rebuild_columns (self);

  spot_update_nav_buttons (self);
  spot_update_title (self);
  spot_update_status (self);
  spot_update_pathbar (self);
  spot_update_action_states (self);
}

void
spot_navigate_to (SpotWindow *self,
                  GFile      *dir,
                  gboolean    push_history)
{
  g_autoptr (GFileInfo) info = NULL;

  if (!dir)
    return;

  info = g_file_query_info (dir,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (!info || g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
    return;

  if (push_history && self->current_dir &&
      !g_file_equal (self->current_dir, dir))
    spot_push_history (self, dir);
  else
    spot_set_current_dir (self, g_object_ref (dir));

  /* Search is scoped to the folder being viewed — leaving it resets it. */
  if (self->search_entry && self->search_text && self->search_text[0])
    gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

  spot_refresh (self);
}

void
spot_window_open_path (SpotWindow *self,
                       const char *path)
{
  g_autoptr (GFile) dir = NULL;

  if (!path || path[0] == '\0')
    return;

  if (g_str_has_prefix (path, "file://") ||
      g_str_has_prefix (path, "trash://"))
    dir = g_file_new_for_uri (path);
  else
    dir = g_file_new_for_path (path);

  spot_navigate_to (self, dir, FALSE);
}

void
spot_window_reveal_uri (SpotWindow   *self,
                        const char   *uri)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GFile) parent = NULL;

  if (!self || !uri || !*uri)
    return;

  file = g_file_new_for_uri (uri);
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL, NULL);
  if (!info)
    return;

  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
      g_autofree char *path = g_file_get_path (file);

      if (path)
        {
          spot_set_view_mode (self, SPOT_VIEW_GRID);
          spot_window_open_path (self, path);
        }
      return;
    }

  parent = g_file_get_parent (file);
  if (!parent)
    return;

  spot_set_view_mode (self, SPOT_VIEW_GRID);
  g_set_object (&self->reveal_target, file);
  spot_navigate_to (self, parent, FALSE);
}

void
spot_navigate_to_path_string (SpotWindow *self,
                              const char *path,
                              gboolean    push_history)
{
  g_autoptr (GFile) dir = g_file_new_for_path (path);

  spot_navigate_to (self, dir, push_history);
}

static void
on_back_clicked (GtkButton *button G_GNUC_UNUSED,
                 gpointer   user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  spot_pop_history (self, &self->back_stack, &self->forward_stack);
  spot_refresh (self);
}

static void
on_forward_clicked (GtkButton *button G_GNUC_UNUSED,
                    gpointer   user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);

  spot_pop_history (self, &self->forward_stack, &self->back_stack);
  spot_refresh (self);
}

static gboolean
spot_search_visible (SpotWindow *self,
                     GFileInfo  *info)
{
  g_autofree char *hay = NULL;
  g_autofree char *needle = NULL;

  if (!self->search_text || !self->search_text[0] || !info)
    return TRUE;

  hay = g_utf8_casefold (g_file_info_get_display_name (info), -1);
  needle = g_utf8_casefold (self->search_text, -1);
  return strstr (hay, needle) != NULL;
}

static gboolean
spot_grid_filter_func (GtkFlowBoxChild *child,
                       gpointer         user_data)
{
  SpotWindow *self = user_data;
  GtkWidget  *inner = gtk_flow_box_child_get_child (child);

  if (!inner || g_object_get_data (G_OBJECT (inner), "spot-loading"))
    return TRUE;

  return spot_search_visible (self,
                              g_object_get_data (G_OBJECT (inner),
                                                 "spot-info"));
}

gboolean
spot_list_filter_func (gpointer item,
                       gpointer user_data)
{
  return spot_search_visible (user_data, G_FILE_INFO (item));
}

static void
on_search_changed (GtkEditable *editable,
                   SpotWindow  *self)
{
  g_free (self->search_text);
  self->search_text = g_strdup (gtk_editable_get_text (editable));

  if (self->grid_flow)
    gtk_flow_box_invalidate_filter (GTK_FLOW_BOX (self->grid_flow));
  if (self->list_filter)
    gtk_filter_changed (self->list_filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static GtkWidget *
spot_create_toolbar (SpotWindow *self)
{
  GtkWidget *toolbar;
  GtkWidget *nav;
  GtkWidget *view;

  toolbar = ooze_toolbar_new ();

  nav = ooze_toolbar_add_group (toolbar);
  self->back_button = spot_create_toolbar_button (spot_icon_back, "Back", "Back",
                                                   FALSE, FALSE);
  gtk_widget_add_css_class (self->back_button, "ooze-nav-btn");
  self->forward_button = spot_create_toolbar_button (spot_icon_forward, "Forward", "Forward",
                                                     FALSE, FALSE);
  gtk_widget_add_css_class (self->forward_button, "ooze-nav-btn");
  gtk_box_append (GTK_BOX (nav), self->back_button);
  gtk_box_append (GTK_BOX (nav), self->forward_button);

  ooze_toolbar_add_separator (toolbar);

  /* Cheetah Finder flow: everything runs from the left on one glyph line —
   * Back/Forward · View · place shortcuts — captions on a shared baseline. */
  view = ooze_toolbar_add_group (toolbar);
  self->view_segment = ooze_segment_group_new ("View");
  ooze_segment_group_add (self->view_segment, spot_icon_view_grid, "Grid view");
  ooze_segment_group_add (self->view_segment, spot_icon_view_list, "List view");
  ooze_segment_group_add (self->view_segment, spot_icon_view_column, "Columns view");
  gtk_box_append (GTK_BOX (view), self->view_segment);

  ooze_toolbar_add_separator (toolbar);

  {
    GtkWidget *places = ooze_toolbar_add_group (toolbar);
    GtkWidget *computer;
    GtkWidget *home;
    GtkWidget *apps;

    computer = spot_create_toolbar_button (spot_icon_computer, "Computer",
                                           "Computer", FALSE, FALSE);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (computer),
                                    "win.go-computer");
    home = spot_create_toolbar_button (spot_icon_home, "Home",
                                       "Home folder", FALSE, FALSE);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (home), "win.go-home");
    apps = spot_create_toolbar_button (spot_icon_applications, "Applications",
                                       "Installed applications", FALSE, FALSE);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (apps),
                                    "win.go-applications");

    gtk_box_append (GTK_BOX (places), computer);
    gtk_box_append (GTK_BOX (places), home);
    gtk_box_append (GTK_BOX (places), apps);
  }

  ooze_toolbar_add_spacer (toolbar);

  self->search_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->search_entry),
                                  "Search this folder");
  gtk_widget_add_css_class (self->search_entry, "ooze-toolbar-search");
  gtk_widget_add_css_class (self->search_entry, "spot-search");
  gtk_widget_set_valign (self->search_entry, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (self->search_entry, GTK_ALIGN_END);
  gtk_widget_set_hexpand (self->search_entry, FALSE);
  gtk_box_append (GTK_BOX (toolbar), self->search_entry);

  g_signal_connect (self->back_button, "clicked", G_CALLBACK (on_back_clicked), self);
  g_signal_connect (self->forward_button, "clicked", G_CALLBACK (on_forward_clicked), self);
  g_signal_connect (self->view_segment, "changed",
                    G_CALLBACK (on_view_segment_changed), self);
  g_signal_connect (self->search_entry, "changed",
                    G_CALLBACK (on_search_changed), self);

  return toolbar;
}

static void
spot_window_constructed (GObject *object)
{
  SpotWindow *self = SPOT_WINDOW (object);
  GtkWidget *shell;
  GtkWidget *content_paned;
  GtkWidget *statusbar;

  G_OBJECT_CLASS (spot_window_parent_class)->constructed (object);

  spot_ensure_css ();

  gtk_window_set_icon_name (GTK_WINDOW (self), "system-file-manager");
  gtk_window_set_default_size (GTK_WINDOW (self), 960, 640);
  /*
   * Do NOT call gtk_window_set_decorated(FALSE).
   * On Wayland, GTK4 uses CSD by default: the Wayland compositor gives
   * us a transparent surface and GTK draws the shadow + rounded frame
   * via the CSS `decoration` node.  We set our OozeHeaderBar as the
   * native titlebar so GTK4 handles drag-to-move and edge-resize
   * automatically – no custom OozeShadowBin machinery needed.
   */
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");
  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Spot");

  spot_install_actions (self);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->toolbar = spot_create_toolbar (self);
  /* ooze_gel_install_drag not needed for the toolbar: GTK4 CSD
   * handles window-move via the titlebar widget above. */

  content_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (content_paned, TRUE);
  gtk_widget_set_vexpand (content_paned, TRUE);

  gtk_paned_set_start_child (GTK_PANED (content_paned), spot_create_sidebar (self));
  gtk_paned_set_resize_start_child (GTK_PANED (content_paned), FALSE);
  gtk_paned_set_shrink_start_child (GTK_PANED (content_paned), FALSE);

  /* ── Columns view ───────────────────────────────────────────────── */
  self->columns_scrolled = ooze_scrolled_window_new ();
  /* Horizontal pan only — each column owns its own vertical map. */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_NEVER);
  gtk_scrolled_window_set_propagate_natural_width (
      GTK_SCROLLED_WINDOW (self->columns_scrolled), FALSE);
  gtk_widget_set_hexpand (self->columns_scrolled, TRUE);
  gtk_widget_set_vexpand (self->columns_scrolled, TRUE);

  self->columns_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                 self->columns_box);

  /* ── Grid / icon view ───────────────────────────────────────────── */
  self->grid_scrolled = ooze_scrolled_window_new ();
  gtk_widget_add_css_class (self->grid_scrolled, "spot-grid-scroll");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->grid_scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->grid_scrolled, TRUE);
  gtk_widget_set_vexpand (self->grid_scrolled, TRUE);

  self->grid_flow = gtk_flow_box_new ();
  gtk_flow_box_set_filter_func (GTK_FLOW_BOX (self->grid_flow),
                                spot_grid_filter_func, self, NULL);
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid_flow),
                                   GTK_SELECTION_SINGLE);
  gtk_flow_box_set_activate_on_single_click (GTK_FLOW_BOX (self->grid_flow), FALSE);
  /* homogeneous = FALSE: rows are only as tall as their tallest cell */
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid_flow), FALSE);
  gtk_flow_box_set_sort_func (GTK_FLOW_BOX (self->grid_flow),
                              spot_grid_sort_func, self, NULL);
  gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->grid_flow), 20);
  gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (self->grid_flow), 2);
  gtk_widget_set_margin_top    (self->grid_flow, 8);
  gtk_widget_set_margin_bottom (self->grid_flow, 8);
  gtk_widget_set_margin_start  (self->grid_flow, 8);
  gtk_widget_set_margin_end    (self->grid_flow, 8);
  /* Don't distribute leftover space — items pack tight from the top-left */
  gtk_widget_set_halign (self->grid_flow, GTK_ALIGN_START);
  gtk_widget_set_valign (self->grid_flow, GTK_ALIGN_START);
  gtk_widget_add_css_class (self->grid_flow, "spot-grid-view");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->grid_scrolled),
                                 self->grid_flow);
  g_signal_connect (self->grid_flow, "child-activated",
                    G_CALLBACK (on_grid_child_activated), self);
  spot_attach_dir_drop (self->grid_flow, self);
  spot_attach_dir_drop (self->grid_scrolled, self);
  spot_attach_context_menu (self, self->grid_flow);
  spot_attach_context_menu (self, self->grid_scrolled);
  spot_attach_context_menu (self, self->columns_scrolled);

  /* ── Stack wraps both views ─────────────────────────────────────── */
  self->content_stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->content_stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration (GTK_STACK (self->content_stack), 100);
  gtk_widget_set_hexpand (self->content_stack, TRUE);
  gtk_widget_set_vexpand (self->content_stack, TRUE);

  gtk_stack_add_named (GTK_STACK (self->content_stack),
                       self->columns_scrolled, "columns");
  gtk_stack_add_named (GTK_STACK (self->content_stack),
                       self->grid_scrolled, "grid");
  self->list_scrolled = spot_create_list_view (self);
  gtk_stack_add_named (GTK_STACK (self->content_stack),
                       self->list_scrolled, "list");
  spot_attach_dir_drop (self->list_scrolled, self);
  spot_attach_context_menu (self, self->list_scrolled);
  gtk_stack_set_visible_child_name (GTK_STACK (self->content_stack), "grid");
  self->view_mode = SPOT_VIEW_GRID;
  self->column_width = SPOT_COLUMN_WIDTH;
  spot_state_load (self);

  gtk_paned_set_end_child (GTK_PANED (content_paned), self->content_stack);

  /* ── Bottom bar: breadcrumbs left, item count right, one strip ── */
  {
    GtkWidget *pathbar_surface;
    GtkWidget *pathbar_scroll;

    pathbar_surface = ooze_surface_new (OOZE_SURFACE_STATUSBAR,
                                        GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class (pathbar_surface, "spot-pathbar");

    pathbar_scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (pathbar_scroll),
                                    GTK_POLICY_EXTERNAL,
                                    GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height (
        GTK_SCROLLED_WINDOW (pathbar_scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width (
        GTK_SCROLLED_WINDOW (pathbar_scroll), FALSE);
    gtk_widget_set_hexpand (pathbar_scroll, TRUE);

    self->pathbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (pathbar_scroll),
                                   self->pathbar);
    gtk_box_append (GTK_BOX (pathbar_surface), pathbar_scroll);

    self->status_label = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (self->status_label), 1.0);
    gtk_widget_set_halign (self->status_label, GTK_ALIGN_END);
    gtk_widget_set_margin_start (self->status_label, 12);
    gtk_widget_add_css_class (self->status_label, "spot-status-count");
    gtk_box_append (GTK_BOX (pathbar_surface), self->status_label);

    self->pathbar_surface = pathbar_surface;
    statusbar = pathbar_surface;
  }

  /* Keep the toolbar in a horizontal scroller so its natural width
   * (Back…Applications + Search) cannot raise the window min-size above
   * half the monitor — Mutter refuses side-tile when min_width is too big.
   */
  {
    GtkWidget *toolbar_scroll = gtk_scrolled_window_new ();

    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                                      TRUE);
    gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                                     FALSE);
    gtk_widget_set_hexpand (toolbar_scroll, TRUE);
    /* Glass rims overhang into toolbar padding — don't clip the strip. */
    gtk_widget_set_overflow (toolbar_scroll, GTK_OVERFLOW_VISIBLE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (toolbar_scroll),
                                   self->toolbar);
    gtk_box_append (GTK_BOX (shell), toolbar_scroll);
  }
  gtk_box_append (GTK_BOX (shell), content_paned);
  gtk_box_append (GTK_BOX (shell), statusbar);

  /* Set content directly – no OozeShadowBin grid wrapper needed. */
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  {
    GtkEventController *keys;

    keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_new_folder_shortcut), self);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);

    keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed",
                      G_CALLBACK (spot_columns_key_pressed), self);
    gtk_widget_add_controller (self->columns_scrolled, keys);
  }

  spot_update_action_states (self);
  spot_append_menus (self);
}

static void
spot_window_dispose (GObject *object)
{
  SpotWindow *self = SPOT_WINDOW (object);

  spot_spring_cancel (self);
  spot_grid_enumeration_cancel (self);
  g_clear_pointer (&self->search_text, g_free);
  g_clear_object (&self->list_filter);
  spot_window_shell_drag_leave (self);
  g_clear_object (&self->current_dir);
  g_clear_object (&self->context_target);
  g_clear_object (&self->reveal_target);
  g_clear_object (&self->list_store);
  if (self->context_menu)
    {
      gtk_widget_unparent (self->context_menu);
      self->context_menu = NULL;
    }
  g_list_free_full (self->back_stack, g_object_unref);
  g_list_free_full (self->forward_stack, g_object_unref);
  self->back_stack = NULL;
  self->forward_stack = NULL;

  G_OBJECT_CLASS (spot_window_parent_class)->dispose (object);
}

static void
spot_window_class_init (SpotWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = spot_window_constructed;
  object_class->dispose = spot_window_dispose;
}

static void
spot_window_init (SpotWindow *self G_GNUC_UNUSED)
{
  /* OozeSurface and OozeButton connect to notify::dark individually, so no
   * whole-window redraw hook is needed here. */
}

SpotWindow *
spot_window_new_for_path (GtkApplication *app,
                          const char     *path)
{
  SpotWindow *window;
  const char *start_path = path;

  window = g_object_new (SPOT_TYPE_WINDOW,
                         "application", app,
                         "standard-edit-actions", FALSE,
                         "standard-menus", FALSE,
                         NULL);

  if (!start_path)
    start_path = g_object_get_data (G_OBJECT (app), "start-path");
  if (!start_path)
    start_path = g_get_home_dir ();

  spot_window_open_path (window, start_path);

  return window;
}

SpotWindow *
spot_window_new (GtkApplication *app)
{
  return spot_window_new_for_path (app, NULL);
}
