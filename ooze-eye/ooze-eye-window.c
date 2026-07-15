#include "ooze-eye-window.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

struct _OozeEyeWindow
{
  OozeApplicationWindow parent_instance;

  GtkWidget *picture;
  GtkWidget *scrolled;
  GtkWidget *status;
  GtkWidget *btn_prev;
  GtkWidget *btn_next;

  GdkPixbuf *pixbuf;
  GFile     *file;
  GPtrArray *siblings; /* GFile* */
  guint      index;
  double     user_zoom; /* 1.0 = fit when fit_mode */
  gboolean   fit_mode;
  int        last_view_w;
  int        last_view_h;
  guint      tick_id;
};

G_DEFINE_FINAL_TYPE (OozeEyeWindow, ooze_eye_window,
                     OOZE_TYPE_APPLICATION_WINDOW)

static void eye_rebuild_siblings (OozeEyeWindow *self);
static void eye_apply_display    (OozeEyeWindow *self);
static void eye_update_chrome    (OozeEyeWindow *self);
static void eye_goto_index       (OozeEyeWindow *self, guint index);
static void ooze_eye_window_constructed (GObject *object);

static const char *const eye_prev_icons[] = { "go-previous", NULL };
static const char *const eye_next_icons[] = { "go-next", NULL };
static const char *const eye_zin_icons[]  = { "zoom-in", NULL };
static const char *const eye_zout_icons[] = { "zoom-out", NULL };
static const char *const eye_fit_icons[]  = { "zoom-fit-best", "zoom-fit", NULL };

static gboolean
eye_is_image_path (const char *path)
{
  const char *dot;

  if (!path)
    return FALSE;
  dot = strrchr (path, '.');
  if (!dot)
    return FALSE;
  return g_ascii_strcasecmp (dot, ".png") == 0 ||
         g_ascii_strcasecmp (dot, ".jpg") == 0 ||
         g_ascii_strcasecmp (dot, ".jpeg") == 0 ||
         g_ascii_strcasecmp (dot, ".gif") == 0 ||
         g_ascii_strcasecmp (dot, ".webp") == 0 ||
         g_ascii_strcasecmp (dot, ".bmp") == 0 ||
         g_ascii_strcasecmp (dot, ".tif") == 0 ||
         g_ascii_strcasecmp (dot, ".tiff") == 0 ||
         g_ascii_strcasecmp (dot, ".svg") == 0;
}

static gboolean
eye_info_is_image (GFileInfo *info, GFile *path)
{
  const char *ctype = g_file_info_get_content_type (info);

  if (ctype && g_content_type_is_a (ctype, "image/*"))
    return TRUE;
  {
    g_autofree char *p = g_file_get_path (path);
    return eye_is_image_path (p);
  }
}

static int
eye_file_strcmp (gconstpointer a, gconstpointer b)
{
  GFile *fa = *(GFile * const *) a;
  GFile *fb = *(GFile * const *) b;
  g_autofree char *na = g_file_get_basename (fa);
  g_autofree char *nb = g_file_get_basename (fb);

  return g_utf8_collate (na ? na : "", nb ? nb : "");
}

static void
eye_rebuild_siblings (OozeEyeWindow *self)
{
  g_autoptr (GFile) parent = NULL;
  g_autoptr (GFileEnumerator) en = NULL;
  guint i;

  if (self->siblings)
    g_ptr_array_set_size (self->siblings, 0);
  else
    self->siblings = g_ptr_array_new_with_free_func (g_object_unref);

  self->index = 0;
  if (!self->file)
    return;

  parent = g_file_get_parent (self->file);
  if (!parent)
    {
      g_ptr_array_add (self->siblings, g_object_ref (self->file));
      return;
    }

  en = g_file_enumerate_children (parent,
                                  G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                  G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL, NULL);
  if (!en)
    {
      g_ptr_array_add (self->siblings, g_object_ref (self->file));
      return;
    }

  for (;;)
    {
      g_autoptr (GFileInfo) info = NULL;
      GFile *path;
      const char *name;

      info = g_file_enumerator_next_file (en, NULL, NULL);
      if (!info)
        break;
      if (g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR)
        continue;

      name = g_file_info_get_name (info);
      path = g_file_get_child (parent, name);
      if (eye_info_is_image (info, path))
        g_ptr_array_add (self->siblings, path);
      else
        g_object_unref (path);
    }
  g_file_enumerator_close (en, NULL, NULL);

  if (self->siblings->len == 0)
    g_ptr_array_add (self->siblings, g_object_ref (self->file));

  g_ptr_array_sort (self->siblings, eye_file_strcmp);

  for (i = 0; i < self->siblings->len; i++)
    {
      if (g_file_equal (self->file, g_ptr_array_index (self->siblings, i)))
        {
          self->index = i;
          break;
        }
    }
}

static void
eye_sync_index (OozeEyeWindow *self)
{
  guint i;

  if (!self->file || !self->siblings)
    return;

  for (i = 0; i < self->siblings->len; i++)
    {
      if (g_file_equal (self->file, g_ptr_array_index (self->siblings, i)))
        {
          self->index = i;
          return;
        }
    }
}

static void
eye_update_chrome (OozeEyeWindow *self)
{
  g_autofree char *base = NULL;
  g_autofree char *title = NULL;
  gboolean has = self->pixbuf != NULL;
  gboolean multi = self->siblings && self->siblings->len > 1;

  if (self->file)
    base = g_file_get_basename (self->file);

  if (base && self->siblings && self->siblings->len > 0)
    title = g_strdup_printf ("%s (%u of %u)",
                             base,
                             self->index + 1,
                             self->siblings->len);
  else if (base)
    title = g_strdup (base);
  else
    title = g_strdup ("Ooze Eye");

  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), title);

  if (self->status)
    {
      if (self->pixbuf)
        {
          g_autofree char *msg = NULL;
          msg = g_strdup_printf ("%d × %d",
                                 gdk_pixbuf_get_width (self->pixbuf),
                                 gdk_pixbuf_get_height (self->pixbuf));
          gtk_label_set_text (GTK_LABEL (self->status), msg);
        }
      else
        gtk_label_set_text (GTK_LABEL (self->status), "No image");
    }

  gtk_widget_set_sensitive (self->btn_prev, has && multi && self->index > 0);
  gtk_widget_set_sensitive (self->btn_next,
                            has && multi &&
                            self->index + 1 < self->siblings->len);
}

static void
eye_apply_display (OozeEyeWindow *self)
{
  int nat_w, nat_h;
  int view_w, view_h;
  double fit;
  double scale;
  int out_w, out_h;
  g_autoptr (GdkPixbuf) scaled = NULL;
  g_autoptr (GdkTexture) texture = NULL;

  if (!self->pixbuf)
    {
      gtk_picture_set_paintable (GTK_PICTURE (self->picture), NULL);
      gtk_widget_set_size_request (self->picture, -1, -1);
      return;
    }

  nat_w = gdk_pixbuf_get_width (self->pixbuf);
  nat_h = gdk_pixbuf_get_height (self->pixbuf);
  if (nat_w < 1 || nat_h < 1)
    return;

  view_w = gtk_widget_get_width (self->scrolled);
  view_h = gtk_widget_get_height (self->scrolled);
  if (view_w < 32)
    view_w = 32;
  if (view_h < 32)
    view_h = 32;
  view_w = MAX (view_w - 8, 32);
  view_h = MAX (view_h - 8, 32);
  self->last_view_w = view_w;
  self->last_view_h = view_h;

  fit = MIN ((double) view_w / (double) nat_w,
             (double) view_h / (double) nat_h);
  if (fit > 1.0)
    fit = 1.0;
  if (fit < 0.01)
    fit = 0.01;

  if (self->fit_mode)
    scale = fit * self->user_zoom;
  else
    scale = self->user_zoom;

  out_w = MAX ((int) (nat_w * scale + 0.5), 1);
  out_h = MAX ((int) (nat_h * scale + 0.5), 1);

  if (out_w == nat_w && out_h == nat_h)
    scaled = g_object_ref (self->pixbuf);
  else
    scaled = gdk_pixbuf_scale_simple (self->pixbuf, out_w, out_h,
                                      GDK_INTERP_BILINEAR);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  texture = gdk_texture_new_for_pixbuf (scaled);
  G_GNUC_END_IGNORE_DEPRECATIONS
  gtk_picture_set_paintable (GTK_PICTURE (self->picture), GDK_PAINTABLE (texture));
  gtk_widget_set_size_request (self->picture, out_w, out_h);
}

static gboolean
eye_tick (GtkWidget     *widget G_GNUC_UNUSED,
          GdkFrameClock *clock G_GNUC_UNUSED,
          gpointer       user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);
  int w, h;

  if (!self->fit_mode || !self->pixbuf)
    return G_SOURCE_CONTINUE;

  w = gtk_widget_get_width (self->scrolled);
  h = gtk_widget_get_height (self->scrolled);
  if (w < 32 || h < 32)
    return G_SOURCE_CONTINUE;

  w = MAX (w - 8, 32);
  h = MAX (h - 8, 32);
  if (w != self->last_view_w || h != self->last_view_h)
    eye_apply_display (self);

  return G_SOURCE_CONTINUE;
}

static void
eye_goto_index (OozeEyeWindow *self, guint index)
{
  GFile *file;

  if (!self->siblings || index >= self->siblings->len)
    return;

  file = g_ptr_array_index (self->siblings, index);
  ooze_eye_window_open_file (self, file);
}

void
ooze_eye_window_open_file (OozeEyeWindow *self,
                           GFile         *file)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *path = NULL;
  GdkPixbuf *pb;
  gboolean same_dir = FALSE;

  g_return_if_fail (OOZE_EYE_IS_WINDOW (self));
  g_return_if_fail (G_IS_FILE (file));

  path = g_file_get_path (file);
  if (!path)
    {
      gtk_label_set_text (GTK_LABEL (self->status), "Cannot open URI");
      return;
    }

  pb = gdk_pixbuf_new_from_file (path, &error);
  if (!pb)
    {
      g_autofree char *msg = g_strdup_printf ("Failed to load: %s",
                                              error ? error->message : "unknown");
      gtk_label_set_text (GTK_LABEL (self->status), msg);
      g_clear_object (&self->pixbuf);
      gtk_picture_set_paintable (GTK_PICTURE (self->picture), NULL);
      return;
    }

  if (self->file)
    {
      g_autoptr (GFile) old_parent = g_file_get_parent (self->file);
      g_autoptr (GFile) new_parent = g_file_get_parent (file);

      same_dir = old_parent && new_parent &&
                 g_file_equal (old_parent, new_parent) &&
                 self->siblings != NULL;
    }

  g_clear_object (&self->pixbuf);
  self->pixbuf = pb;
  g_clear_object (&self->file);
  self->file = g_object_ref (file);

  if (same_dir)
    {
      eye_sync_index (self);
    }
  else
    {
      self->user_zoom = 1.0;
      self->fit_mode = TRUE;
      eye_rebuild_siblings (self);
    }

  eye_apply_display (self);
  eye_update_chrome (self);
}

static void
eye_action_about (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *param G_GNUC_UNUSED,
                  gpointer       user_data)
{
  ooze_about_present (GTK_WINDOW (user_data),
                      "Ooze Eye",
                      "image-x-generic",
                      "Image viewer for Ooze Desktop.",
                      OOZE_VERSION);
}

static void
on_open_finish (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file = NULL;

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, &error);
  if (!file)
    return;
  ooze_eye_window_open_file (self, file);
}

static void
eye_action_open (GSimpleAction *action G_GNUC_UNUSED,
                 GVariant      *param G_GNUC_UNUSED,
                 gpointer       user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);
  GtkFileDialog *dialog;
  g_autoptr (GtkFileFilter) filter = NULL;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Open Image");
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Images");
  gtk_file_filter_add_mime_type (filter, "image/*");
  gtk_file_dialog_set_default_filter (dialog, filter);

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL,
                        on_open_finish, self);
  g_object_unref (dialog);
}

static void
eye_action_zoom_in (GSimpleAction *a G_GNUC_UNUSED,
                    GVariant *p G_GNUC_UNUSED,
                    gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  self->user_zoom *= 1.25;
  if (self->user_zoom > 32.0)
    self->user_zoom = 32.0;
  eye_apply_display (self);
}

static void
eye_action_zoom_out (GSimpleAction *a G_GNUC_UNUSED,
                     GVariant *p G_GNUC_UNUSED,
                     gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  self->user_zoom /= 1.25;
  if (self->user_zoom < 0.05)
    self->user_zoom = 0.05;
  eye_apply_display (self);
}

static void
eye_action_zoom_fit (GSimpleAction *a G_GNUC_UNUSED,
                     GVariant *p G_GNUC_UNUSED,
                     gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  self->fit_mode = TRUE;
  self->user_zoom = 1.0;
  eye_apply_display (self);
}

static void
eye_action_zoom_100 (GSimpleAction *a G_GNUC_UNUSED,
                     GVariant *p G_GNUC_UNUSED,
                     gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  self->fit_mode = FALSE;
  self->user_zoom = 1.0;
  eye_apply_display (self);
}

static void
eye_action_prev (GSimpleAction *a G_GNUC_UNUSED,
                 GVariant *p G_GNUC_UNUSED,
                 gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  if (self->index > 0)
    eye_goto_index (self, self->index - 1);
}

static void
eye_action_next (GSimpleAction *a G_GNUC_UNUSED,
                 GVariant *p G_GNUC_UNUSED,
                 gpointer user_data)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (user_data);

  if (self->siblings && self->index + 1 < self->siblings->len)
    eye_goto_index (self, self->index + 1);
}

static GMenuModel *
eye_build_file_menu (void)
{
  GMenu *file = g_menu_new ();

  g_menu_append (file, "Open…", "win.open");
  g_menu_append (file, "Close Window", "win.close");
  return G_MENU_MODEL (file);
}

static GMenuModel *
eye_build_view_menu (void)
{
  GMenu *view = g_menu_new ();

  g_menu_append (view, "Zoom In", "win.zoom-in");
  g_menu_append (view, "Zoom Out", "win.zoom-out");
  g_menu_append (view, "Actual Size", "win.zoom-100");
  g_menu_append (view, "Fit to Window", "win.zoom-fit");
  g_menu_append (view, "Previous Image", "win.prev");
  g_menu_append (view, "Next Image", "win.next");
  return G_MENU_MODEL (view);
}

static GMenuModel *
eye_build_help_menu (void)
{
  GMenu *help = g_menu_new ();

  g_menu_append (help, "About Ooze Eye", "win.about");
  return G_MENU_MODEL (help);
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                guint                  keyval,
                guint                  keycode G_GNUC_UNUSED,
                GdkModifierType         state G_GNUC_UNUSED,
                OozeEyeWindow         *self)
{
  switch (keyval)
    {
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_KP_Add:
      g_action_group_activate_action (G_ACTION_GROUP (self), "zoom-in", NULL);
      return TRUE;
    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
      g_action_group_activate_action (G_ACTION_GROUP (self), "zoom-out", NULL);
      return TRUE;
    case GDK_KEY_0:
    case GDK_KEY_KP_0:
      g_action_group_activate_action (G_ACTION_GROUP (self), "zoom-100", NULL);
      return TRUE;
    case GDK_KEY_f:
      g_action_group_activate_action (G_ACTION_GROUP (self), "zoom-fit", NULL);
      return TRUE;
    case GDK_KEY_Left:
    case GDK_KEY_Page_Up:
      g_action_group_activate_action (G_ACTION_GROUP (self), "prev", NULL);
      return TRUE;
    case GDK_KEY_Right:
    case GDK_KEY_Page_Down:
    case GDK_KEY_space:
      g_action_group_activate_action (G_ACTION_GROUP (self), "next", NULL);
      return TRUE;
    case GDK_KEY_Escape:
      gtk_window_close (GTK_WINDOW (self));
      return TRUE;
    default:
      return FALSE;
    }
}

static void
ooze_eye_window_dispose (GObject *object)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (object);

  if (self->tick_id)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick_id);
      self->tick_id = 0;
    }

  g_clear_object (&self->pixbuf);
  g_clear_object (&self->file);
  g_clear_pointer (&self->siblings, g_ptr_array_unref);

  G_OBJECT_CLASS (ooze_eye_window_parent_class)->dispose (object);
}

static void
ooze_eye_window_class_init (OozeEyeWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ooze_eye_window_constructed;
  object_class->dispose = ooze_eye_window_dispose;
}

static void
ooze_eye_window_init (OozeEyeWindow *self)
{
  static const GActionEntry entries[] = {
    { .name = "about",    .activate = eye_action_about },
    { .name = "open",     .activate = eye_action_open },
    { .name = "zoom-in",  .activate = eye_action_zoom_in },
    { .name = "zoom-out", .activate = eye_action_zoom_out },
    { .name = "zoom-fit", .activate = eye_action_zoom_fit },
    { .name = "zoom-100", .activate = eye_action_zoom_100 },
    { .name = "prev",     .activate = eye_action_prev },
    { .name = "next",     .activate = eye_action_next },
  };
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   entries, G_N_ELEMENTS (entries),
                                   self);
}

static void
ooze_eye_window_constructed (GObject *object)
{
  OozeEyeWindow *self = OOZE_EYE_WINDOW (object);
  GMenuModel *file;
  GMenuModel *view;
  GMenuModel *help;
  GtkWidget *shell;
  GtkWidget *toolbar;
  GtkWidget *group;
  GtkWidget *surface;
  GtkEventController *keys;
  GtkWidget *btn;

  G_OBJECT_CLASS (ooze_eye_window_parent_class)->constructed (object);

  self->user_zoom = 1.0;
  self->fit_mode = TRUE;

  ooze_toolbar_ensure_css ();

  gtk_window_set_default_size (GTK_WINDOW (self), 900, 640);
  gtk_window_set_icon_name (GTK_WINDOW (self), "image-x-generic");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-eye");
  /* Same Gel CSD class as Spot/King/Pak — corners + shadow from ooze_theme_ensure. */
  gtk_widget_add_css_class (GTK_WIDGET (self), "spot-finder");

  ooze_application_window_set_title (
    OOZE_APPLICATION_WINDOW (self), "Ooze Eye");

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  toolbar = ooze_toolbar_new ();
  group = ooze_toolbar_add_group (toolbar);

  self->btn_prev = ooze_button_new_toolbar (
    eye_prev_icons, "Previous", "Previous image");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (self->btn_prev), "win.prev");
  gtk_box_append (GTK_BOX (group), self->btn_prev);

  self->btn_next = ooze_button_new_toolbar (
    eye_next_icons, "Next", "Next image");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (self->btn_next), "win.next");
  gtk_box_append (GTK_BOX (group), self->btn_next);

  ooze_toolbar_add_separator (toolbar);
  group = ooze_toolbar_add_group (toolbar);

  btn = ooze_button_new_toolbar (eye_zout_icons, "Smaller", "Zoom out");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (btn), "win.zoom-out");
  gtk_box_append (GTK_BOX (group), btn);

  btn = ooze_button_new_toolbar (eye_zin_icons, "Larger", "Zoom in");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (btn), "win.zoom-in");
  gtk_box_append (GTK_BOX (group), btn);

  btn = ooze_button_new_toolbar (eye_fit_icons, "Fit", "Fit to window");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (btn), "win.zoom-fit");
  gtk_box_append (GTK_BOX (group), btn);

  ooze_toolbar_add_spacer (toolbar);
  self->status = gtk_label_new ("No image");
  gtk_widget_set_margin_end (self->status, 12);
  gtk_box_append (GTK_BOX (toolbar), self->status);

  gtk_box_append (GTK_BOX (shell), toolbar);

  /* Opaque OozeKit fill behind the image — ooze-scrolled is transparent. */
  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (surface, TRUE);
  gtk_widget_set_vexpand (surface, TRUE);

  self->scrolled = ooze_scrolled_window_new ();
  gtk_widget_set_hexpand (self->scrolled, TRUE);
  gtk_widget_set_vexpand (self->scrolled, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  self->picture = gtk_picture_new ();
  gtk_picture_set_can_shrink (GTK_PICTURE (self->picture), FALSE);
  gtk_picture_set_content_fit (GTK_PICTURE (self->picture),
                               GTK_CONTENT_FIT_FILL);
  gtk_widget_set_halign (self->picture, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->picture, GTK_ALIGN_CENTER);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled),
                                 self->picture);

  gtk_box_append (GTK_BOX (surface), self->scrolled);
  gtk_box_append (GTK_BOX (shell), surface);
  ooze_application_window_set_content (
    OOZE_APPLICATION_WINDOW (self), shell);

  self->tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (self),
                                                eye_tick, self, NULL);

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  eye_update_chrome (self);

  file = eye_build_file_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "File", file);
  g_object_unref (file);
  view = eye_build_view_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "View", view);
  g_object_unref (view);
  help = eye_build_help_menu ();
  ooze_application_window_append_menu_section (
    OOZE_APPLICATION_WINDOW (self), "Help", help);
  g_object_unref (help);
}

GtkWidget *
ooze_eye_window_new (GtkApplication *app)
{
  return GTK_WIDGET (g_object_new (OOZE_EYE_TYPE_WINDOW,
                                   "application", app,
                                   NULL));
}
