#include "ooze-defaults-pane.h"

#include "ooze-scroll.h"
#include "ooze-surface.h"

#include <gio/gio.h>

typedef struct
{
  const char *label;
  const char * const *types;
  GtkWidget *dropdown;
  GList *apps;
} DefaultCategory;

struct _OozeDefaultsPane
{
  GtkBox parent_instance;
  DefaultCategory categories[8];
};

G_DEFINE_FINAL_TYPE (OozeDefaultsPane, ooze_defaults_pane, GTK_TYPE_BOX)

static const char * const browser_types[] = {
  "x-scheme-handler/http",
  "x-scheme-handler/https",
  NULL,
};
static const char * const email_types[] = {
  "x-scheme-handler/mailto",
  NULL,
};
static const char * const file_manager_types[] = {
  "inode/directory",
  NULL,
};
static const char * const image_types[] = {
  "image/jpeg",
  "image/png",
  "image/gif",
  "image/webp",
  "image/bmp",
  "image/tiff",
  "image/svg+xml",
  NULL,
};
static const char * const audio_types[] = {
  "audio/mpeg",
  "audio/flac",
  "audio/x-vorbis+ogg",
  "audio/x-wav",
  NULL,
};
static const char * const video_types[] = {
  "video/mp4",
  "video/x-matroska",
  "video/webm",
  NULL,
};
static const char * const text_types[] = {
  "text/plain",
  NULL,
};
static const char * const torrent_types[] = {
  "application/x-bittorrent",
  "x-scheme-handler/magnet",
  NULL,
};

static GtkWidget *
make_row (const char *title,
          GtkWidget  *control)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *label = gtk_label_new (title);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (row), label);
  gtk_box_append (GTK_BOX (row), control);
  return row;
}

static gint
compare_app_info (gconstpointer a,
                  gconstpointer b)
{
  const char *a_name = g_app_info_get_display_name (G_APP_INFO (a));
  const char *b_name = g_app_info_get_display_name (G_APP_INFO (b));

  return g_utf8_collate (a_name ? a_name : "", b_name ? b_name : "");
}

static guint
find_app (GList    *apps,
          GAppInfo *target)
{
  guint index = 0;

  for (GList *link = apps; link; link = link->next, index++)
    {
      if (target && g_app_info_equal (G_APP_INFO (link->data), target))
        return index;
    }

  return GTK_INVALID_LIST_POSITION;
}

static void
append_apps_for_type (GList      **apps,
                      const char  *type)
{
  GList *type_apps;

  type_apps = g_app_info_get_all_for_type (type);
  for (GList *link = type_apps; link; link = link->next)
    {
      gboolean duplicate = FALSE;

      for (GList *existing = *apps; existing; existing = existing->next)
        {
          if (g_app_info_equal (G_APP_INFO (existing->data),
                                G_APP_INFO (link->data)))
            {
              duplicate = TRUE;
              break;
            }
        }

      if (duplicate)
        g_object_unref (link->data);
      else
        *apps = g_list_prepend (*apps, link->data);
    }
  g_list_free (type_apps);
}

static void
set_category_model (DefaultCategory *category)
{
  GtkStringList *model;
  g_autoptr (GAppInfo) default_app = NULL;
  guint selected = GTK_INVALID_LIST_POSITION;

  for (guint i = 0; category->types[i]; i++)
    append_apps_for_type (&category->apps, category->types[i]);
  category->apps = g_list_sort (category->apps, compare_app_info);

  model = gtk_string_list_new (NULL);
  for (GList *link = category->apps; link; link = link->next)
    {
      const char *name =
        g_app_info_get_display_name (G_APP_INFO (link->data));

      gtk_string_list_append (model, name ? name : "Unnamed application");
    }

  if (!category->apps)
    {
      gtk_string_list_append (model, "None available");
      gtk_widget_set_sensitive (category->dropdown, FALSE);
    }
  else
    {
      /* Prefer the category's primary type, but recover if only a
       * secondary association has a persisted default. */
      for (guint i = 0; category->types[i] &&
           selected == GTK_INVALID_LIST_POSITION; i++)
        {
          default_app =
            g_app_info_get_default_for_type (category->types[i], FALSE);
          if (default_app)
            selected = find_app (category->apps, default_app);
          g_clear_object (&default_app);
        }
    }

  gtk_drop_down_set_model (GTK_DROP_DOWN (category->dropdown),
                           G_LIST_MODEL (model));
  gtk_drop_down_set_selected (GTK_DROP_DOWN (category->dropdown), selected);
  g_object_unref (model);
}

static void
on_category_changed (GtkDropDown    *dropdown,
                     GParamSpec     *pspec G_GNUC_UNUSED,
                     DefaultCategory *category)
{
  guint selected;
  GAppInfo *app;

  selected = gtk_drop_down_get_selected (dropdown);
  if (selected == GTK_INVALID_LIST_POSITION || !category->apps)
    return;

  app = g_list_nth_data (category->apps, selected);
  if (!app)
    return;

  for (guint i = 0; category->types[i]; i++)
    {
      g_autoptr (GError) error = NULL;

      if (!g_app_info_set_as_default_for_type (app, category->types[i],
                                               &error))
        {
          g_warning ("Ooze Defaults: failed to set %s for %s: %s",
                     g_app_info_get_display_name (app),
                     category->types[i],
                     error ? error->message : "unknown error");
        }
    }

  for (guint i = 0; category->types[i]; i++)
    {
      g_autoptr (GAppInfo) persisted =
        g_app_info_get_default_for_type (category->types[i], FALSE);

      if (!persisted || !g_app_info_equal (persisted, app))
        g_warning ("Ooze Defaults: %s was not persisted for %s",
                   g_app_info_get_display_name (app),
                   category->types[i]);
    }
}

static void
ooze_defaults_pane_dispose (GObject *object)
{
  OozeDefaultsPane *self = OOZE_DEFAULTS_PANE (object);

  for (guint i = 0; i < G_N_ELEMENTS (self->categories); i++)
    {
      g_list_free_full (self->categories[i].apps, g_object_unref);
      self->categories[i].apps = NULL;
    }

  G_OBJECT_CLASS (ooze_defaults_pane_parent_class)->dispose (object);
}

static void
ooze_defaults_pane_class_init (OozeDefaultsPaneClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_defaults_pane_dispose;
}

static void
ooze_defaults_pane_init (OozeDefaultsPane *self)
{
  static const struct
  {
    const char *label;
    const char * const *types;
  } definitions[] = {
    { "Web Browser", browser_types },
    { "Email", email_types },
    { "File Manager", file_manager_types },
    { "Image Viewer", image_types },
    { "Audio", audio_types },
    { "Video", video_types },
    { "Text Editor", text_types },
    { "Torrent", torrent_types },
  };
  GtkWidget *scroll;
  GtkWidget *surface;
  GtkWidget *box;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_VERTICAL);

  scroll = ooze_scrolled_window_new ();
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_box_append (GTK_BOX (self), scroll);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (surface, 16);
  gtk_widget_set_margin_end (surface, 16);
  gtk_widget_set_margin_top (surface, 16);
  gtk_widget_set_margin_bottom (surface, 16);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), surface);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_box_append (GTK_BOX (surface), box);

  for (guint i = 0; i < G_N_ELEMENTS (definitions); i++)
    {
      DefaultCategory *category = &self->categories[i];

      category->label = definitions[i].label;
      category->types = definitions[i].types;
      category->dropdown = gtk_drop_down_new (NULL, NULL);
      gtk_widget_set_size_request (category->dropdown, 280, -1);
      gtk_box_append (GTK_BOX (box),
                      make_row (category->label, category->dropdown));
      set_category_model (category);
      g_signal_connect (category->dropdown, "notify::selected",
                        G_CALLBACK (on_category_changed), category);
    }
}

GtkWidget *
ooze_defaults_pane_new (void)
{
  return g_object_new (OOZE_DEFAULTS_TYPE_PANE, NULL);
}
