#include "ooze-shot.h"

#include "ooze-notifications.h"
#include "ooze-plugin-priv.h"

#include <clutter/clutter.h>
#include <cogl/cogl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <meta/display.h>
#include <mtk/mtk-rectangle.h>

#include <stdio.h>
#include <string.h>

#define OOZE_SHOT_BUS_NAME "org.ooze.Shell.Screenshot"
#define OOZE_SHOT_OBJECT_PATH "/org/ooze/Shell/Screenshot"
#define OOZE_SHOT_INTERFACE "org.ooze.Shell.Screenshot"

struct _OozeShot
{
  OozePlugin      *plugin; /* borrowed; owned by the plugin lifecycle */
  GDBusNodeInfo   *introspection;
  GDBusConnection  *connection;
  guint            registration_id;
  guint            own_name_id;
};

static const char shot_introspection_xml[] =
  "<node>"
  "  <interface name='org.ooze.Shell.Screenshot'>"
  "    <method name='CaptureDesktop'>"
  "      <arg type='s' name='path' direction='out'/>"
  "    </method>"
  "    <method name='CaptureArea'>"
  "      <arg type='i' name='x' direction='in'/>"
  "      <arg type='i' name='y' direction='in'/>"
  "      <arg type='i' name='width' direction='in'/>"
  "      <arg type='i' name='height' direction='in'/>"
  "      <arg type='s' name='path' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static void
ooze_shot_free_pixels (guchar  *pixels,
                       gpointer user_data G_GNUC_UNUSED)
{
  g_free (pixels);
}

static ClutterStage *
ooze_shot_get_stage (OozeShot *shot)
{
  MetaDisplay *display;
  MetaContext *context;
  MetaBackend *backend;

  display = meta_plugin_get_display (META_PLUGIN (shot->plugin));
  if (!display)
    return NULL;
  context = meta_display_get_context (display);
  if (!context)
    return NULL;
  backend = meta_context_get_backend (context);
  if (!backend)
    return NULL;
  return CLUTTER_STAGE (meta_backend_get_stage (backend));
}

static char *
ooze_shot_picture_path (void)
{
  const char *pictures;
  g_autofree char *fallback_pictures = NULL;
  g_autoptr (GDateTime) now = NULL;
  g_autofree char *stamp = NULL;
  g_autofree char *directory = NULL;
  g_autofree char *filename = NULL;

  pictures = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
  if (!pictures || !pictures[0])
    {
      fallback_pictures = g_build_filename (g_get_home_dir (), "Pictures", NULL);
      pictures = fallback_pictures;
    }
  directory = g_build_filename (pictures, "Screenshots", NULL);
  if (g_mkdir_with_parents (directory, 0700) != 0)
    return NULL;

  now = g_date_time_new_now_local ();
  stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
  filename = g_strdup_printf ("Screenshot-%s.png", stamp);
  return g_build_filename (directory, filename, NULL);
}

static gboolean
ooze_shot_capture_area (OozeShot      *shot,
                        gint           x,
                        gint           y,
                        gint           width,
                        gint           height,
                        char         **path,
                        GError       **error)
{
  ClutterStage *stage;
  g_autofree guchar *pixels = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autofree char *filename = NULL;
  MtkRectangle capture_rect;
  gint stage_width;
  gint stage_height;
  gint pixel_width;
  gint pixel_height;
  gsize rowstride;
  gsize pixel_bytes;
  gfloat scale;
  g_autoptr (GError) capture_error = NULL;

  g_return_val_if_fail (path != NULL, FALSE);
  *path = NULL;

  stage = ooze_shot_get_stage (shot);
  if (!stage)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Ooze stage is unavailable");
      return FALSE;
    }

  stage_width = (gint) clutter_actor_get_width (CLUTTER_ACTOR (stage));
  stage_height = (gint) clutter_actor_get_height (CLUTTER_ACTOR (stage));
  if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x > stage_width - width || y > stage_height - height)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Capture area is outside the stage");
      return FALSE;
    }

  capture_rect = MTK_RECTANGLE_INIT (x, y, width, height);
  if (!clutter_stage_get_capture_final_size (stage,
                                             &capture_rect,
                                             &pixel_width,
                                             &pixel_height,
                                             &scale) ||
      pixel_width <= 0 || pixel_height <= 0 || scale <= 0.0f)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not determine the Ooze capture size");
      return FALSE;
    }

  rowstride = (gsize) pixel_width * 4;
  if (pixel_width > 0 && rowstride / 4 != (gsize) pixel_width)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Ooze capture dimensions are too large");
      return FALSE;
    }
  if (rowstride > G_MAXINT ||
      (pixel_height > 0 && rowstride > G_MAXSIZE / (gsize) pixel_height))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Ooze capture buffer is too large");
      return FALSE;
    }
  pixel_bytes = rowstride * (gsize) pixel_height;

  pixels = g_malloc0 (pixel_bytes);
  if (!clutter_stage_paint_to_buffer (stage,
                                       &capture_rect,
                                       scale,
                                       pixels,
                                       (int) rowstride,
                                       COGL_PIXEL_FORMAT_RGBA_8888,
                                       NULL,
                                       CLUTTER_PAINT_FLAG_NONE,
                                       &capture_error))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not capture the Ooze stage: %s",
                   capture_error ? capture_error->message : "unknown error");
      return FALSE;
    }

  pixbuf = gdk_pixbuf_new_from_data (pixels,
                                     GDK_COLORSPACE_RGB,
                                     TRUE,
                                     8,
                                     pixel_width,
                                     pixel_height,
                                     (int) rowstride,
                                     ooze_shot_free_pixels,
                                     NULL);
  if (!pixbuf)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not create an image from captured pixels");
      return FALSE;
    }
  g_steal_pointer (&pixels);

  filename = ooze_shot_picture_path ();
  if (!filename)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not create the Screenshots directory");
      return FALSE;
    }
  if (!gdk_pixbuf_save (pixbuf, filename, "png", error, NULL))
    return FALSE;

  *path = g_steal_pointer (&filename);
  return TRUE;
}

static void
ooze_shot_handle_capture (OozeShot             *shot,
                          GDBusMethodInvocation *invocation,
                          gint                  x,
                          gint                  y,
                          gint                  width,
                          gint                  height)
{
  g_autofree char *path = NULL;
  g_autoptr (GError) error = NULL;

  if (!ooze_shot_capture_area (shot, x, y, width, height, &path, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  ooze_notifications_show (shot->plugin->notifications,
                           "Screenshot saved", path);
  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)",
                                                                     path));
}

static void
ooze_shot_method_call (GDBusConnection       *connection G_GNUC_UNUSED,
                       const char            *sender G_GNUC_UNUSED,
                       const char            *object_path G_GNUC_UNUSED,
                       const char            *interface_name G_GNUC_UNUSED,
                       const char            *method_name,
                       GVariant              *parameters,
                       GDBusMethodInvocation  *invocation,
                       gpointer               user_data)
{
  OozeShot *shot = user_data;
  ClutterStage *stage;
  gint width;
  gint height;

  if (g_strcmp0 (method_name, "CaptureDesktop") == 0)
    {
      stage = ooze_shot_get_stage (shot);
      if (!stage)
        {
          g_dbus_method_invocation_return_error (
            invocation, G_IO_ERROR, G_IO_ERROR_FAILED,
            "Ooze stage is unavailable");
          return;
        }
      width = (gint) clutter_actor_get_width (CLUTTER_ACTOR (stage));
      height = (gint) clutter_actor_get_height (CLUTTER_ACTOR (stage));
      ooze_shot_handle_capture (shot, invocation, 0, 0, width, height);
      return;
    }
  if (g_strcmp0 (method_name, "CaptureArea") == 0)
    {
      gint x;
      gint y;

      g_variant_get (parameters, "(iiii)", &x, &y, &width, &height);
      ooze_shot_handle_capture (shot, invocation, x, y, width, height);
      return;
    }

  g_dbus_method_invocation_return_error (invocation,
                                         G_IO_ERROR,
                                         G_IO_ERROR_NOT_SUPPORTED,
                                         "Unknown Ooze Shot method %s",
                                         method_name);
}

static const GDBusInterfaceVTable shot_vtable = {
  ooze_shot_method_call,
  NULL,
  NULL,
  { NULL },
};

static void
ooze_shot_bus_acquired (GDBusConnection *connection,
                        const char      *name G_GNUC_UNUSED,
                        gpointer         user_data)
{
  OozeShot *shot = user_data;
  g_autoptr (GError) error = NULL;

  shot->connection = g_object_ref (connection);
  shot->registration_id =
    g_dbus_connection_register_object (connection,
                                       OOZE_SHOT_OBJECT_PATH,
                                       shot->introspection->interfaces[0],
                                       &shot_vtable,
                                       shot,
                                       NULL,
                                       &error);
  if (!shot->registration_id)
    g_warning ("Ooze Shot: could not register D-Bus object: %s",
               error ? error->message : "unknown");
}

static void
ooze_shot_name_acquired (GDBusConnection *connection G_GNUC_UNUSED,
                         const char      *name G_GNUC_UNUSED,
                         gpointer         user_data G_GNUC_UNUSED)
{
  g_print ("Ooze Shot: D-Bus server ready\n");
}

static void
ooze_shot_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                     const char      *name G_GNUC_UNUSED,
                     gpointer         user_data)
{
  OozeShot *shot = user_data;

  if (shot->connection && shot->registration_id)
    {
      g_dbus_connection_unregister_object (shot->connection,
                                           shot->registration_id);
      shot->registration_id = 0;
    }
}

OozeShot *
ooze_shot_new (OozePlugin *plugin)
{
  OozeShot *shot;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (OOZE_IS_PLUGIN (plugin), NULL);

  shot = g_new0 (OozeShot, 1);
  shot->plugin = plugin;
  shot->introspection =
    g_dbus_node_info_new_for_xml (shot_introspection_xml, &error);
  if (!shot->introspection)
    {
      g_warning ("Ooze Shot: invalid D-Bus introspection: %s",
                 error ? error->message : "unknown");
      ooze_shot_free (shot);
      return NULL;
    }

  shot->own_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    OOZE_SHOT_BUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    ooze_shot_bus_acquired,
                    ooze_shot_name_acquired,
                    ooze_shot_name_lost,
                    shot,
                    NULL);
  return shot;
}

gboolean
ooze_shot_capture_desktop (OozeShot *shot,
                           GError  **error)
{
  ClutterStage *stage;
  gint width;
  gint height;
  g_autofree char *path = NULL;

  g_return_val_if_fail (shot != NULL, FALSE);
  stage = ooze_shot_get_stage (shot);
  if (!stage)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Ooze stage is unavailable");
      return FALSE;
    }

  width = (gint) clutter_actor_get_width (CLUTTER_ACTOR (stage));
  height = (gint) clutter_actor_get_height (CLUTTER_ACTOR (stage));
  if (!ooze_shot_capture_area (shot, 0, 0, width, height, &path, error))
    return FALSE;

  ooze_notifications_show (shot->plugin->notifications,
                           "Screenshot saved", path);
  return TRUE;
}

void
ooze_shot_free (OozeShot *shot)
{
  if (!shot)
    return;

  if (shot->own_name_id)
    {
      g_bus_unown_name (shot->own_name_id);
      shot->own_name_id = 0;
    }
  if (shot->connection && shot->registration_id)
    {
      g_dbus_connection_unregister_object (shot->connection,
                                           shot->registration_id);
      shot->registration_id = 0;
    }
  g_clear_object (&shot->connection);
  g_clear_pointer (&shot->introspection, g_dbus_node_info_unref);
  g_free (shot);
}
