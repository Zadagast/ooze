#include "spot-window.h"

#include "my-icons.h"

#include "ooze-header-bar.h"
#include "ooze-window-chrome.h"

/* OozeKit — shared surface + button widgets */
#include "ooze-surface.h"
#include "ooze-button.h"

#include <string.h>

#define SPOT_COLUMN_WIDTH     180
#define SPOT_MIN_COLUMNS       3
#define SPOT_LIST_ICON_SIZE   16
#define SPOT_GRID_ICON_SIZE   56
#define SPOT_GRID_CELL_WIDTH  90
#define SPOT_TOOLBAR_ICON_SIZE 32
#define SPOT_SIDEBAR_ICON_SIZE 32

typedef enum {
  SPOT_VIEW_COLUMNS = 0,
  SPOT_VIEW_GRID    = 1,
  SPOT_VIEW_LIST    = 2,   /* same as columns for now; reserved */
} SpotViewMode;

struct _SpotWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *title_header;
  GtkWidget *toolbar;
  GtkWidget *search_entry;

  /* content stack — switches between view modes */
  GtkWidget *content_stack;

  /* column view */
  GtkWidget *columns_scrolled;
  GtkWidget *columns_box;

  /* grid / icon view */
  GtkWidget *grid_scrolled;
  GtkWidget *grid_flow;       /* GtkFlowBox */

  GtkWidget *sidebar;
  GtkWidget *status_label;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *grid_view_button;
  GtkWidget *list_view_button;
  GtkWidget *column_view_button;
  GtkWidget *new_folder_popover;
  GtkWidget *new_folder_entry;

  GFile *current_dir;
  GList *back_stack;
  GList *forward_stack;
  guint last_column_count;
  SpotViewMode view_mode;
};

G_DEFINE_FINAL_TYPE (SpotWindow, spot_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct
{
  const char *label;
  GUserDirectory dir;
  gboolean use_home;
} SpotPlace;

static const SpotPlace sidebar_places[] = {
  { "Home", 0, TRUE },
  { "Desktop", G_USER_DIRECTORY_DESKTOP, FALSE },
  { "Documents", G_USER_DIRECTORY_DOCUMENTS, FALSE },
  { "Downloads", G_USER_DIRECTORY_DOWNLOAD, FALSE },
  { "Pictures", G_USER_DIRECTORY_PICTURES, FALSE },
  { "Music", G_USER_DIRECTORY_MUSIC, FALSE },
  { "Videos", G_USER_DIRECTORY_VIDEOS, FALSE },
};

/* ══════════════════════════════════════════════════════════════════════════ */

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

  my_icons_configure_gtk ();

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

                                     /* ── Toolbar layout ── */
                                     ".spot-toolbar {"
                                     "  background: none;"
                                     "  min-height: 52px;"
                                     "  padding: 4px 8px;"
                                     "}"
                                     ".spot-toolbar .spot-toolbar-group { padding: 0 4px; }"

                                     /* Separator tint adapts via Adwaita's border token */
                                     ".spot-toolbar separator {"
                                     "  background: @borders;"
                                     "  min-width: 1px;"
                                     "  margin: 4px 2px;"
                                     "}"

                                     /* ── Labeled toolbar buttons — sizing only
                                      * OozeButton (.ooze-button) strips the GTK chrome;
                                      * we add the Spot-specific min-width here. */
                                     ".spot-finder-btn { min-width: 52px; }"
                                     ".spot-finder-btn:active { color: #ffffff; }"
                                     ".spot-finder-btn-label {"
                                     "  font-size: 10px;"
                                     "  font-weight: 500;"
                                     "}"

                                     /* Icon-only nav/view buttons */
                                     ".spot-finder-icon-btn {"
                                     "  min-width: 28px;"
                                     "  min-height: 26px;"
                                     "  padding: 2px 4px;"
                                     "}"
                                     ".spot-finder-icon-btn image { -gtk-icon-size: 16px; }"

                                     /* ── Search field ─────────────────────────────────
                                      * Shape + size only.  Background, border, text colour
                                      * and focus ring are left to Adwaita so they adapt
                                      * automatically between light and dark mode.
                                      * ─────────────────────────────────────────────────── */
                                     ".spot-toolbar .spot-search {"
                                     "  min-width: 155px;"
                                     "  border-radius: 10px;"
                                     "  font-size: 11px;"
                                     "}"

                                     /* ── Sidebar ── */
                                     ".spot-sidebar-list { background: none; }"
                                     ".spot-sidebar-list row { padding: 5px 6px; }"
                                     ".spot-sidebar-list row:hover { background: rgba(128,128,128,0.10); }"
                                     ".spot-sidebar-list row:selected {"
                                     "  background: #2968c8;"
                                     "}"
                                     ".spot-sidebar-list .spot-sidebar-label {"
                                     "  font-size: 11px;"
                                     "  font-weight: 500;"
                                     "  color: @sidebar_fg_color;"
                                     "}"
                                     ".spot-sidebar-list row:selected .spot-sidebar-label {"
                                     "  color: #ffffff;"
                                     "}"

                                     /* ── Column browser — adaptive colours via Adwaita tokens ── */
                                     ".spot-column {"
                                     "  min-width: 180px;"
                                     "  border-right: 1px solid @borders;"
                                     "  background: @view_bg_color;"
                                     "}"
                                     ".spot-column row { padding: 2px 8px; font-size: 11px;"
                                     "                   color: @view_fg_color; }"
                                     ".spot-column row:hover { background: rgba(41,104,200,0.10); }"
                                     ".spot-column row:selected {"
                                     "  background: #2968c8;"
                                     "  color: #ffffff;"
                                     "}"

                                     /* ── Status bar ── */
                                     ".spot-statusbar {"
                                     "  background: none;"
                                     "  padding: 3px 10px;"
                                     "  font-size: 11px;"
                                     "  font-weight: 400;"
                                     "  color: @window_fg_color;"
                                     "  opacity: 0.65;"
                                     "}"

                                     /* ── Grid / icon view ────────────────────────────────── *
                                      * Use @headerbar_bg_color so the content area matches the
                                      * Ooze dark-charcoal surface in dark mode (~#303030) and
                                      * a light neutral in light mode — both matching our palette.
                                      * ─────────────────────────────────────────────────────── */

                                     /* Scrolled window + its inner viewport: uniform background */
                                     ".spot-grid-scroll {"
                                     "  background: @headerbar_bg_color;"
                                     "}"
                                     ".spot-grid-scroll > viewport {"
                                     "  background: transparent;"
                                     "}"

                                     /* FlowBox itself: transparent so the scroll bg shows through */
                                     ".spot-grid-view {"
                                     "  background: transparent;"
                                     "}"

                                     /* ── Grid / icon view — visual tokens only ──────────────
                                      * All spacing and margins are set in C via
                                      * gtk_widget_set_margin_* so CSS stays visual-only.
                                      * ─────────────────────────────────────────────────── */

                                     /* Selection + hover chrome on the FlowBoxChild */
                                     ".spot-grid-view > flowboxchild {"
                                     "  border-radius: 5px;"
                                     "  outline: none;"
                                     "  background: none;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected {"
                                     "  background: #2968c8;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:hover:not(:selected) {"
                                     "  background: rgba(41,104,200,0.10);"
                                     "}"

                                     /* Filename label — colour follows window fg, not view fg */
                                     ".spot-grid-label {"
                                     "  font-size: 11px;"
                                     "  color: @window_fg_color;"
                                     "}"
                                     ".spot-grid-view > flowboxchild:selected .spot-grid-label {"
                                     "  color: #ffffff;"
                                     "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);

  loaded = TRUE;
}

static void spot_on_column_row_activated (GtkListBox    *box,
                                         GtkListBoxRow *row,
                                         gpointer       user_data);
static void spot_refresh (SpotWindow *self);
static void spot_on_new_folder_create (GtkButton  *button,
                                       SpotWindow *self);
static void spot_show_error (SpotWindow  *self,
                             const char  *heading,
                             const char  *body);
static void spot_show_new_folder_popover (SpotWindow *self);
static void spot_navigate_to_path_string (SpotWindow *self,
                                          const char *path,
                                          gboolean push_history);
static void spot_navigate_to (SpotWindow *self, GFile *dir, gboolean push_history);
static void spot_open_file (GFile *file);
static void spot_rebuild_columns (SpotWindow *self);
static gint spot_compare_file_info (gconstpointer a, gconstpointer b);

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

static GtkWidget *
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
  return image;
}

/* ── Grid-view helpers ──────────────────────────────────────────────────── */

static GtkWidget *
spot_create_grid_cell (GFileInfo *info, GFile *file)
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
  return box;
}

static void
spot_clear_grid (SpotWindow *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->grid_flow)) != NULL)
    gtk_flow_box_remove (GTK_FLOW_BOX (self->grid_flow), child);
}

static void
spot_populate_grid (SpotWindow *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GList *entries = NULL;
  GList *l;
  GFileInfo *info;

  spot_clear_grid (self);

  if (!self->current_dir)
    return;

  enumerator = g_file_enumerate_children (
      self->current_dir,
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
    return;

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      if (g_file_info_get_is_hidden (info))
        g_object_unref (info);
      else
        entries = g_list_prepend (entries, info);
    }

  entries = g_list_sort (entries, spot_compare_file_info);

  for (l = entries; l != NULL; l = l->next)
    {
      GFileInfo *entry = l->data;
      g_autoptr (GFile) child = g_file_get_child (self->current_dir,
                                                   g_file_info_get_name (entry));
      GtkWidget *cell = spot_create_grid_cell (entry, child);

      gtk_flow_box_append (GTK_FLOW_BOX (self->grid_flow), cell);

      /* Pin the auto-created GtkFlowBoxChild so it never stretches */
      {
        GtkWidget *fbc = gtk_widget_get_parent (cell);
        if (fbc)
          {
            gtk_widget_set_margin_top    (fbc, 0);
            gtk_widget_set_margin_bottom (fbc, 0);
            gtk_widget_set_margin_start  (fbc, 0);
            gtk_widget_set_margin_end    (fbc, 0);
            gtk_widget_set_valign (fbc, GTK_ALIGN_START);
          }
      }
    }

  g_list_free_full (entries, g_object_unref);
}

static void
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

/* ── View-mode switching ────────────────────────────────────────────────── */

static void
spot_set_view_mode (SpotWindow *self, SpotViewMode mode)
{
  self->view_mode = mode;

  /* Sync .active class on the three toggle buttons */
  if (mode == SPOT_VIEW_GRID)
    gtk_widget_add_css_class (self->grid_view_button, "active");
  else
    gtk_widget_remove_css_class (self->grid_view_button, "active");

  if (mode == SPOT_VIEW_LIST)
    gtk_widget_add_css_class (self->list_view_button, "active");
  else
    gtk_widget_remove_css_class (self->list_view_button, "active");

  if (mode == SPOT_VIEW_COLUMNS)
    gtk_widget_add_css_class (self->column_view_button, "active");
  else
    gtk_widget_remove_css_class (self->column_view_button, "active");

  /* Flip the stack page */
  gtk_stack_set_visible_child_name (
      GTK_STACK (self->content_stack),
      mode == SPOT_VIEW_GRID ? "grid" : "columns");

  /* Repopulate whichever view is now visible */
  if (mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else
    spot_rebuild_columns (self);
}

static void
on_grid_view_clicked (GtkButton *btn G_GNUC_UNUSED, SpotWindow *self)
{
  spot_set_view_mode (self, SPOT_VIEW_GRID);
}

static void
on_list_view_clicked (GtkButton *btn G_GNUC_UNUSED, SpotWindow *self)
{
  /* Treat list as columns for now */
  spot_set_view_mode (self, SPOT_VIEW_COLUMNS);
}

static void
on_column_view_clicked (GtkButton *btn G_GNUC_UNUSED, SpotWindow *self)
{
  spot_set_view_mode (self, SPOT_VIEW_COLUMNS);
}

static GtkWidget *
spot_image_new_from_icon_list (const char * const *icon_names,
                               int                   size,
                               gboolean              prefer_color)
{
  GtkWidget *image;
  g_autoptr (GtkIconPaintable) paintable = NULL;
  GtkIconTheme *theme;
  gsize i;

  image = gtk_image_new ();
  theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());

  if (prefer_color)
    {
      for (i = 0; icon_names && icon_names[i]; i++)
        {
          paintable = gtk_icon_theme_lookup_icon (theme,
                                                  icon_names[i],
                                                  NULL,
                                                  size,
                                                  1,
                                                  GTK_TEXT_DIR_LTR,
                                                  GTK_ICON_LOOKUP_PRELOAD);
          if (paintable)
            break;
        }
    }

  if (!paintable)
    {
      for (i = 0; icon_names && icon_names[i]; i++)
        {
          paintable = gtk_icon_theme_lookup_icon (theme,
                                                  icon_names[i],
                                                  NULL,
                                                  size,
                                                  1,
                                                  GTK_TEXT_DIR_LTR,
                                                  GTK_ICON_LOOKUP_PRELOAD |
                                                  GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
          if (paintable)
            break;
        }
    }

  if (paintable)
    gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (paintable));

  gtk_image_set_pixel_size (GTK_IMAGE (image), size);
  return image;
}

static const char * const spot_icon_back[] = {
  "go-previous", "go-previous-symbolic", NULL
};
static const char * const spot_icon_forward[] = {
  "go-next", "go-next-symbolic", NULL
};
static const char * const spot_icon_view_grid[] = {
  "view-grid-symbolic", "view-grid", NULL
};
static const char * const spot_icon_view_list[] = {
  "view-list-symbolic", "view-list", NULL
};
static const char * const spot_icon_view_column[] = {
  "view-column-symbolic", "view-dual-symbolic", NULL
};
static const char * const spot_icon_computer[] = {
  "computer", "drive-harddisk", NULL
};
static const char * const spot_icon_home[] = {
  "user-home", "go-home", NULL
};
static const char * const spot_icon_favorites[] = {
  "folder-documents", "folder", NULL
};
static const char * const spot_icon_applications[] = {
  "application-default-icon", "application-x-executable", "system-run", NULL
};
static const char * const spot_icon_drive[] = {
  "drive-harddisk", "drive-harddisk-symbolic", NULL
};

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
spot_create_icon_button (const char * const *icon_names,
                         const char         *tooltip,
                         gboolean            toggle,
                         gboolean            active)
{
  GtkWidget *button;
  GtkWidget *image;
  int icon_size = toggle ? SPOT_TOOLBAR_ICON_SIZE : 24;

  button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  gtk_widget_add_css_class (button, "spot-finder-icon-btn");
  if (toggle)
    {
      gtk_widget_add_css_class (button, "spot-finder-toggle");
      if (active)
        gtk_widget_add_css_class (button, "active");
    }

  image = spot_image_new_from_icon_list (icon_names, icon_size, TRUE);
  gtk_button_set_child (GTK_BUTTON (button), image);
  gtk_widget_set_tooltip_text (button, tooltip);
  return button;
}

static GtkWidget *
spot_create_labeled_toolbar_button (const char         *label,
                                    const char * const *icon_names,
                                    const char         *tooltip)
{
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *lbl;

  button = ooze_button_new (OOZE_BUTTON_TOOLBAR);
  gtk_widget_add_css_class (button, "spot-finder-btn");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
  image = spot_image_new_from_icon_list (icon_names, SPOT_TOOLBAR_ICON_SIZE, TRUE);
  lbl = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.5);
  gtk_widget_add_css_class (lbl, "spot-finder-btn-label");
  gtk_box_append (GTK_BOX (box), image);
  gtk_box_append (GTK_BOX (box), lbl);
  gtk_button_set_child (GTK_BUTTON (button), box);
  gtk_widget_set_tooltip_text (button, tooltip);

  return button;
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
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  image = spot_image_new_from_icon_list (icon_names, SPOT_SIDEBAR_ICON_SIZE, TRUE);
  lbl = gtk_label_new (label);
  gtk_label_set_xalign (GTK_LABEL (lbl), 0.5);
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

static void
on_nav_computer_clicked (GtkButton *button G_GNUC_UNUSED,
                         gpointer   user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), "/", TRUE);
}

static void
on_nav_home_clicked (GtkButton *button G_GNUC_UNUSED,
                       gpointer   user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), g_get_home_dir (), TRUE);
}

static void
on_nav_favorites_clicked (GtkButton *button G_GNUC_UNUSED,
                            gpointer   user_data)
{
  const char *path = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);

  if (!path)
    path = g_get_home_dir ();
  spot_navigate_to_path_string (SPOT_WINDOW (user_data), path, TRUE);
}

static void
on_nav_applications_clicked (GtkButton *button G_GNUC_UNUSED,
                             gpointer   user_data)
{
  spot_navigate_to_path_string (SPOT_WINDOW (user_data),
                                "/usr/share/applications",
                                TRUE);
}

static void
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

static guint
spot_max_columns_for_width (int width)
{
  guint by_space;

  if (width < SPOT_COLUMN_WIDTH)
    return SPOT_MIN_COLUMNS;

  by_space = (guint) (width / SPOT_COLUMN_WIDTH);
  if (by_space <= SPOT_MIN_COLUMNS)
    return SPOT_MIN_COLUMNS;

  return by_space;
}

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

static void
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

static void
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

static void
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

static int
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

static GFile *
spot_column_browser_root (GFile *dir)
{
  g_autoptr (GFile) best = NULL;
  gsize i;

  if (!dir)
    return NULL;

  /* Home and special dirs first — longest / deepest match wins. */
  for (i = 0; i < G_N_ELEMENTS (sidebar_places); i++)
    {
      const char *path;
      g_autoptr (GFile) place = NULL;

      if (sidebar_places[i].use_home)
        path = g_get_home_dir ();
      else
        path = g_get_user_special_dir (sidebar_places[i].dir);
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
      ooze_header_bar_set_title (OOZE_HEADER_BAR (self->title_header), "Spot");
      return;
    }

  {
    g_autofree char *basename = g_file_get_basename (self->current_dir);
    const char *title = basename;

    if (!title || title[0] == '\0')
      title = "/";

    ooze_header_bar_set_title (OOZE_HEADER_BAR (self->title_header), title);
    gtk_window_set_title (GTK_WINDOW (self), title);
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

static void
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

  if (!g_app_info_launch_default_for_uri (uri, NULL, &error))
    g_warning ("Spot: failed to open %s: %s", uri, error->message);
}

static void
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

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled, SPOT_COLUMN_WIDTH, -1);
  gtk_widget_add_css_class (scrolled, "spot-column");

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
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
      gtk_list_box_append (GTK_LIST_BOX (list), row);

      if (select_child && g_file_equal (child, select_child))
        selected_row = row;
    }

  g_list_free_full (entries, g_object_unref);

  if (selected_row)
    gtk_list_box_select_row (GTK_LIST_BOX (list), GTK_LIST_BOX_ROW (selected_row));

  g_signal_connect (list, "row-activated", G_CALLBACK (spot_on_column_row_activated), self);

  return scrolled;
}

static void
spot_clear_columns (SpotWindow *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->columns_box)) != NULL)
    gtk_box_remove (GTK_BOX (self->columns_box), child);
}

static void
spot_rebuild_columns (SpotWindow *self)
{
  g_autoptr (GFile) root = NULL;
  g_autolist (GFile) chain = NULL;
  guint len;
  guint i;
  guint max_columns;
  guint start;
  int width;

  spot_clear_columns (self);

  if (!self->current_dir)
    return;

  root = spot_column_browser_root (self->current_dir);
  chain = spot_path_chain_from_root (self->current_dir, root);
  len = g_list_length (chain);
  if (len == 0)
    return;

  width = gtk_widget_get_width (self->columns_scrolled);
  if (width <= 0)
    width = SPOT_COLUMN_WIDTH * SPOT_MIN_COLUMNS;

  max_columns = spot_max_columns_for_width (width);
  if (max_columns > len)
    max_columns = len;

  start = len - max_columns;
  self->last_column_count = max_columns;

  for (i = start; i < len; i++)
    {
      GFile *dir = g_list_nth_data (chain, i);
      GFile *select = (i + 1 < len) ? g_list_nth_data (chain, i + 1) : NULL;
      GtkWidget *column;

      column = spot_create_column_list (self, dir, select);
      gtk_box_append (GTK_BOX (self->columns_box), column);
    }

  spot_scroll_columns_to_end (self);
}

static void
spot_on_columns_width_changed (GObject    *object G_GNUC_UNUSED,
                               GParamSpec *pspec G_GNUC_UNUSED,
                               SpotWindow *self)
{
  int width;
  guint max_columns;

  if (!self->current_dir)
    return;

  width = gtk_widget_get_width (self->columns_scrolled);
  max_columns = spot_max_columns_for_width (width);

  if (max_columns == self->last_column_count)
    return;

  spot_rebuild_columns (self);
}

static void
spot_refresh (SpotWindow *self)
{
  if (self->view_mode == SPOT_VIEW_GRID)
    spot_populate_grid (self);
  else
    spot_rebuild_columns (self);

  spot_update_nav_buttons (self);
  spot_update_title (self);
  spot_update_status (self);
}

static void
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

static void
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

static void
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

static void
on_sidebar_row_activated (GtkListBox    *box G_GNUC_UNUSED,
                          GtkListBoxRow *row,
                          gpointer       user_data)
{
  SpotWindow *self = SPOT_WINDOW (user_data);
  const char *path = g_object_get_data (G_OBJECT (row), "spot-path");

  if (path)
    spot_navigate_to_path_string (self, path, TRUE);
}

static GtkWidget *
spot_create_sidebar (SpotWindow *self)
{
  GtkWidget *bin;
  GtkWidget *scrolled;
  GtkWidget *list;
  gsize i;

  /* OozeSurface draws the sidebar chrome; the scrolled window inside is
   * transparent so the surface colour shows through. */
  bin = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (bin, 88, -1);

  scrolled = gtk_scrolled_window_new ();
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
                       spot_create_sidebar_row ("Linux HD",
                                                "/",
                                                spot_icon_drive));

  for (i = 0; i < G_N_ELEMENTS (sidebar_places); i++)
    {
      const char *path;

      if (sidebar_places[i].use_home)
        path = g_get_home_dir ();
      else
        path = g_get_user_special_dir (sidebar_places[i].dir);
      if (!path)
        continue;

      gtk_list_box_append (GTK_LIST_BOX (list),
                           spot_create_sidebar_row (sidebar_places[i].label,
                                                    path,
                                                    spot_sidebar_icon_names (&sidebar_places[i])));
    }

  g_signal_connect (list, "row-activated", G_CALLBACK (on_sidebar_row_activated), self);
  self->sidebar = list;

  return bin;
}

static GtkWidget *
spot_create_toolbar (SpotWindow *self)
{
  GtkWidget *toolbar;
  GtkWidget *sep;
  GtkWidget *spacer;
  GtkWidget *computer_btn;
  GtkWidget *home_btn;
  GtkWidget *favorites_btn;
  GtkWidget *applications_btn;

  toolbar = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (toolbar), 2);
  gtk_widget_add_css_class (toolbar, "spot-toolbar");

  self->back_button = spot_create_icon_button (spot_icon_back, "Back", FALSE, FALSE);
  self->forward_button = spot_create_icon_button (spot_icon_forward, "Forward", FALSE, FALSE);
  gtk_box_append (GTK_BOX (toolbar), self->back_button);
  gtk_box_append (GTK_BOX (toolbar), self->forward_button);

  sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (sep, 6);
  gtk_widget_set_margin_end (sep, 6);
  gtk_box_append (GTK_BOX (toolbar), sep);

  self->grid_view_button =
    spot_create_icon_button (spot_icon_view_grid, "Icon View", TRUE, FALSE);
  gtk_box_append (GTK_BOX (toolbar), self->grid_view_button);

  self->list_view_button =
    spot_create_icon_button (spot_icon_view_list, "List View", TRUE, FALSE);
  gtk_box_append (GTK_BOX (toolbar), self->list_view_button);

  self->column_view_button =
    spot_create_icon_button (spot_icon_view_column, "Column View", TRUE, TRUE);
  gtk_box_append (GTK_BOX (toolbar), self->column_view_button);

  sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (sep, 6);
  gtk_widget_set_margin_end (sep, 6);
  gtk_box_append (GTK_BOX (toolbar), sep);

  computer_btn = spot_create_labeled_toolbar_button ("Computer",
                                                     spot_icon_computer,
                                                     "Computer");
  home_btn = spot_create_labeled_toolbar_button ("Home",
                                                 spot_icon_home,
                                                 "Home");
  favorites_btn = spot_create_labeled_toolbar_button ("Favorites",
                                                      spot_icon_favorites,
                                                      "Favorites");
  applications_btn = spot_create_labeled_toolbar_button ("Applications",
                                                         spot_icon_applications,
                                                         "Applications");
  gtk_box_append (GTK_BOX (toolbar), computer_btn);
  gtk_box_append (GTK_BOX (toolbar), home_btn);
  gtk_box_append (GTK_BOX (toolbar), favorites_btn);
  gtk_box_append (GTK_BOX (toolbar), applications_btn);

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (toolbar), spacer);

  self->search_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->search_entry), "Search");
  gtk_widget_add_css_class (self->search_entry, "spot-search");
  gtk_widget_set_size_request (self->search_entry, 190, -1);
  gtk_widget_set_margin_top    (self->search_entry, 9);
  gtk_widget_set_margin_bottom (self->search_entry, 9);
  gtk_widget_set_margin_start  (self->search_entry, 8);
  gtk_widget_set_margin_end    (self->search_entry, 6);
  gtk_widget_set_halign (self->search_entry, GTK_ALIGN_END);
  gtk_box_append (GTK_BOX (toolbar), self->search_entry);

  g_signal_connect (self->back_button, "clicked", G_CALLBACK (on_back_clicked), self);
  g_signal_connect (self->forward_button, "clicked", G_CALLBACK (on_forward_clicked), self);
  g_signal_connect (self->grid_view_button, "clicked", G_CALLBACK (on_grid_view_clicked), self);
  g_signal_connect (self->list_view_button, "clicked", G_CALLBACK (on_list_view_clicked), self);
  g_signal_connect (self->column_view_button, "clicked", G_CALLBACK (on_column_view_clicked), self);
  g_signal_connect (computer_btn, "clicked", G_CALLBACK (on_nav_computer_clicked), self);
  g_signal_connect (home_btn, "clicked", G_CALLBACK (on_nav_home_clicked), self);
  g_signal_connect (favorites_btn, "clicked", G_CALLBACK (on_nav_favorites_clicked), self);
  g_signal_connect (applications_btn, "clicked", G_CALLBACK (on_nav_applications_clicked), self);

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

  gtk_window_set_title (GTK_WINDOW (self), "Spot");
  gtk_window_set_icon_name (GTK_WINDOW (self), "org.ooze.Spot");
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

  /* OozeHeaderBar becomes the native CSD titlebar widget.
   * GTK4 automatically enables drag-to-move and double-click-to-maximize
   * on it, and marks it with the "titlebar" CSS class. */
  self->title_header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->title_header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->title_header), "Spot");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->title_header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->toolbar = spot_create_toolbar (self);
  /* ooze_window_install_drag not needed for the toolbar: GTK4 CSD
   * handles window-move via the titlebar widget above. */

  content_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (content_paned, TRUE);
  gtk_widget_set_vexpand (content_paned, TRUE);

  gtk_paned_set_start_child (GTK_PANED (content_paned), spot_create_sidebar (self));
  gtk_paned_set_resize_start_child (GTK_PANED (content_paned), FALSE);
  gtk_paned_set_shrink_start_child (GTK_PANED (content_paned), FALSE);

  /* ── Columns view ───────────────────────────────────────────────── */
  self->columns_scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->columns_scrolled, TRUE);
  gtk_widget_set_vexpand (self->columns_scrolled, TRUE);

  self->columns_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->columns_scrolled),
                                 self->columns_box);
  g_signal_connect (self->columns_scrolled,
                    "notify::width",
                    G_CALLBACK (spot_on_columns_width_changed),
                    self);

  /* ── Grid / icon view ───────────────────────────────────────────── */
  self->grid_scrolled = gtk_scrolled_window_new ();
  gtk_widget_add_css_class (self->grid_scrolled, "spot-grid-scroll");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->grid_scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (self->grid_scrolled, TRUE);
  gtk_widget_set_vexpand (self->grid_scrolled, TRUE);

  self->grid_flow = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->grid_flow),
                                   GTK_SELECTION_SINGLE);
  /* homogeneous = FALSE: rows are only as tall as their tallest cell */
  gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (self->grid_flow), FALSE);
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

  gtk_paned_set_end_child (GTK_PANED (content_paned), self->content_stack);

  statusbar = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_add_css_class (statusbar, "spot-statusbar");
  self->status_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->status_label), 0.0);
  gtk_box_append (GTK_BOX (statusbar), self->status_label);

  /* title_header is the CSD titlebar (set above), NOT a shell child. */
  gtk_box_append (GTK_BOX (shell), self->toolbar);
  gtk_box_append (GTK_BOX (shell), content_paned);
  gtk_box_append (GTK_BOX (shell), statusbar);

  /* Set content directly – no OozeShadowBin grid wrapper needed. */
  gtk_window_set_child (GTK_WINDOW (self), shell);

  {
    GtkEventController *keys;

    keys = gtk_event_controller_key_new ();
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_new_folder_shortcut), self);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);
  }
}

static void
spot_window_dispose (GObject *object)
{
  SpotWindow *self = SPOT_WINDOW (object);

  g_clear_object (&self->current_dir);
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
spot_window_new_for_path (AdwApplication *app,
                          const char    *path)
{
  SpotWindow *window;
  const char *start_path = path;

  window = g_object_new (SPOT_TYPE_WINDOW,
                           "application", app,
                           NULL);

  if (!start_path)
    start_path = g_object_get_data (G_OBJECT (app), "start-path");
  if (!start_path)
    start_path = g_get_home_dir ();

  spot_window_open_path (window, start_path);

  return window;
}

SpotWindow *
spot_window_new (AdwApplication *app)
{
  return spot_window_new_for_path (app, NULL);
}
