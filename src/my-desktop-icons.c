#include "my-desktop-icons.h"

#include "my-aqua-draw.h"
#include "my-dock.h"

#include <meta/window.h>

#define DESKTOP_ICON_SIZE   48.0f
#define DESKTOP_ICON_GAP    72.0f
#define DESKTOP_ICON_MARGIN 24.0f
#define DESKTOP_TOP_INSET   28.0f
#define DESKTOP_DOUBLE_MS   400

typedef struct
{
  const char *label;
  const char *path;
  const char *const *icon_names;
  gfloat icon_r;
  gfloat icon_g;
  gfloat icon_b;
} MyDesktopIconDef;

typedef struct
{
  MetaContext *context;
  MetaDisplay *display;
  guint32 last_click_time;
  char *last_click_path;
} MyDesktopIconData;

static const char *linux_hd_icon_names[] = {
  "drive-harddisk",
  "drive-harddisk-symbolic",
  NULL,
};

static const char *home_icon_names[] = {
  "user-home",
  "user-home-symbolic",
  "go-home",
  NULL,
};

static const MyDesktopIconDef desktop_icon_defs[] = {
  { "Linux HD", "/", linux_hd_icon_names, 0.85f, 0.22f, 0.18f },
  { NULL, NULL, home_icon_names, 0.22f, 0.48f, 0.92f },
};

static void
my_desktop_icon_data_free (gpointer data)
{
  MyDesktopIconData *icon_data = data;

  g_free (icon_data->last_click_path);
  g_free (icon_data);
}

static void
my_desktop_icon_launch (MyDesktopIconData *icon_data,
                        const char        *path)
{
  if (!icon_data || !icon_data->context)
    return;

  if (!path || path[0] == '\0')
    path = g_get_home_dir ();

  my_dock_launch_spot_path (icon_data->context, path);
}

static gboolean
my_desktop_icon_pressed (ClutterActor *actor,
                         ClutterEvent *event,
                         MyDesktopIconData *icon_data)
{
  const char *path;
  guint32 time;
  guint interval;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  path = g_object_get_data (G_OBJECT (actor), "desktop-path");
  if (!path)
    return CLUTTER_EVENT_PROPAGATE;

  time = clutter_event_get_time (event);
  interval = time - icon_data->last_click_time;

  if (icon_data->last_click_path &&
      g_strcmp0 (icon_data->last_click_path, path) == 0 &&
      interval < DESKTOP_DOUBLE_MS)
    {
      my_desktop_icon_launch (icon_data, path);
      g_clear_pointer (&icon_data->last_click_path, g_free);
      icon_data->last_click_time = 0;
      return CLUTTER_EVENT_STOP;
    }

  g_free (icon_data->last_click_path);
  icon_data->last_click_path = g_strdup (path);
  icon_data->last_click_time = time;

  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
my_desktop_icon_create (ClutterActor       *ref_actor,
                        MetaDisplay        *display,
                        MyDesktopIconData  *icon_data,
                        const char         *label,
                        const char         *path,
                        const char * const *icon_names,
                        gfloat              r,
                        gfloat              g,
                        gfloat              b)
{
  ClutterActor *icon;
  ClutterActor *label_actor;
  g_autoptr (ClutterContent) icon_content = NULL;
  g_autoptr (ClutterContent) label_content = NULL;
  int label_w = 1;
  int label_h = 1;
  int texture;

  icon = clutter_actor_new ();
  clutter_actor_set_reactive (icon, TRUE);
  clutter_actor_set_size (icon, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE + 18.0f);
  g_object_set_data_full (G_OBJECT (icon),
                          "desktop-path",
                          g_strdup (path),
                          g_free);

  texture = my_aqua_icon_texture_size (display, (int) DESKTOP_ICON_SIZE);
  if (icon_names)
    icon_content = my_dock_themed_icon_content (ref_actor,
                                                display,
                                                icon_names,
                                                (int) DESKTOP_ICON_SIZE);
  if (!icon_content)
    icon_content = my_aqua_dock_icon_content (ref_actor,
                                              (int) DESKTOP_ICON_SIZE,
                                              r,
                                              g,
                                              b);
  if (icon_content)
    {
      ClutterActor *image = clutter_actor_new ();
      my_aqua_actor_set_scaled_content (image,
                                        g_steal_pointer (&icon_content),
                                        (int) DESKTOP_ICON_SIZE,
                                        (int) DESKTOP_ICON_SIZE,
                                        texture,
                                        texture);
      clutter_actor_add_child (icon, image);
      clutter_actor_show (image);
    }

  label_content = my_aqua_text_content (ref_actor,
                                        "Sans Bold 9",
                                        label,
                                        1.0f,
                                        1.0f,
                                        1.0f,
                                        &label_w,
                                        &label_h);
  label_actor = clutter_actor_new ();
  if (label_content)
    my_aqua_actor_set_content (label_actor,
                               g_steal_pointer (&label_content),
                               label_w,
                               label_h);
  clutter_actor_set_position (label_actor,
                              (DESKTOP_ICON_SIZE - label_w) / 2.0f,
                              DESKTOP_ICON_SIZE + 2.0f);
  clutter_actor_add_child (icon, label_actor);
  clutter_actor_show (label_actor);

  g_signal_connect (icon,
                    "button-press-event",
                    G_CALLBACK (my_desktop_icon_pressed),
                    icon_data);

  return icon;
}

ClutterActor *
my_desktop_icons_create (MetaContext  *context,
                         MetaDisplay  *display,
                         ClutterActor *ref_actor,
                         int           monitor G_GNUC_UNUSED,
                         int           width,
                         int           height)
{
  ClutterActor *container;
  MyDesktopIconData *icon_data;
  gsize i;
  gfloat x;
  gfloat y;
  g_autofree char *home_label = NULL;

  container = clutter_actor_new ();
  clutter_actor_set_reactive (container, FALSE);

  icon_data = g_new0 (MyDesktopIconData, 1);
  icon_data->context = context;
  icon_data->display = display;
  g_object_set_data_full (G_OBJECT (container),
                          "desktop-icon-data",
                          icon_data,
                          my_desktop_icon_data_free);

  home_label = g_strdup_printf ("%s", g_get_user_name ());

  x = (gfloat) width - DESKTOP_ICON_SIZE - DESKTOP_ICON_MARGIN;
  y = DESKTOP_TOP_INSET;

  for (i = 0; i < G_N_ELEMENTS (desktop_icon_defs); i++)
    {
      ClutterActor *icon;
      const char *label;
      const char *path;
      g_autofree char *home_path = NULL;

      if (desktop_icon_defs[i].path)
        {
          label = desktop_icon_defs[i].label;
          path = desktop_icon_defs[i].path;
        }
      else
        {
          label = home_label;
          home_path = g_strdup (g_get_home_dir ());
          path = home_path;
        }

      icon = my_desktop_icon_create (ref_actor,
                                     display,
                                     icon_data,
                                     label,
                                     path,
                                     desktop_icon_defs[i].icon_names,
                                     desktop_icon_defs[i].icon_r,
                                     desktop_icon_defs[i].icon_g,
                                     desktop_icon_defs[i].icon_b);
      clutter_actor_set_position (icon, x, y);
      clutter_actor_add_child (container, icon);
      clutter_actor_show (icon);

      y += DESKTOP_ICON_GAP;
    }

  clutter_actor_set_size (container, (gfloat) width, (gfloat) height);
  return container;
}
