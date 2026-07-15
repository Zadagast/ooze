#include "ooze-application-window.h"

#include "ooze-window-actions.h"

#include <adwaita.h>

typedef struct
{
  OozeHeaderBar *header;
  GMenu *menubar;
  gboolean standard_edit_actions;
  gboolean standard_menus;
} OozeApplicationWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (OozeApplicationWindow, ooze_application_window,
                            GTK_TYPE_APPLICATION_WINDOW)

enum
{
  PROP_0,
  PROP_STANDARD_EDIT_ACTIONS,
  PROP_STANDARD_MENUS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

static void
ooze_application_window_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  OozeApplicationWindowPrivate *priv;

  priv = ooze_application_window_get_instance_private (
    OOZE_APPLICATION_WINDOW (object));

  switch (property_id)
    {
    case PROP_STANDARD_EDIT_ACTIONS:
      g_value_set_boolean (value, priv->standard_edit_actions);
      break;
    case PROP_STANDARD_MENUS:
      g_value_set_boolean (value, priv->standard_menus);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
ooze_application_window_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  OozeApplicationWindowPrivate *priv;

  priv = ooze_application_window_get_instance_private (
    OOZE_APPLICATION_WINDOW (object));

  switch (property_id)
    {
    case PROP_STANDARD_EDIT_ACTIONS:
      priv->standard_edit_actions = g_value_get_boolean (value);
      break;
    case PROP_STANDARD_MENUS:
      priv->standard_menus = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
ooze_application_window_dispose (GObject *object)
{
  OozeApplicationWindowPrivate *priv;

  priv = ooze_application_window_get_instance_private (
    OOZE_APPLICATION_WINDOW (object));
  g_clear_object (&priv->menubar);

  G_OBJECT_CLASS (ooze_application_window_parent_class)->dispose (object);
}

static void
ooze_application_window_setup (OozeApplicationWindow *self)
{
  OozeApplicationWindowPrivate *priv;
  GtkApplication *application;

  priv = ooze_application_window_get_instance_private (self);
  application = gtk_window_get_application (GTK_WINDOW (self));
  g_return_if_fail (application != NULL);

  ooze_window_actions_add_chrome (GTK_APPLICATION_WINDOW (self));
  if (priv->standard_edit_actions)
    ooze_window_actions_add_edit (GTK_APPLICATION_WINDOW (self));

  priv->header = ooze_header_bar_new ();
  ooze_header_bar_attach_window (priv->header, GTK_WINDOW (self));
  gtk_window_set_titlebar (GTK_WINDOW (self), GTK_WIDGET (priv->header));

  priv->menubar = g_menu_new ();
  if (priv->standard_menus)
    {
      ooze_menubar_append_edit (priv->menubar);
      ooze_menubar_append_window (priv->menubar);
    }
  gtk_application_set_menubar (application, G_MENU_MODEL (priv->menubar));
  gtk_application_window_set_show_menubar (
    GTK_APPLICATION_WINDOW (self), FALSE);
}

static void
ooze_application_window_constructed (GObject *object)
{
  G_OBJECT_CLASS (ooze_application_window_parent_class)->constructed (object);

  ooze_application_window_setup (OOZE_APPLICATION_WINDOW (object));
}

static void
ooze_application_window_class_init (OozeApplicationWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  properties[PROP_STANDARD_EDIT_ACTIONS] =
    g_param_spec_boolean ("standard-edit-actions",
                          "Standard edit actions",
                          "Install the standard Ooze edit actions",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  properties[PROP_STANDARD_MENUS] =
    g_param_spec_boolean ("standard-menus",
                          "Standard menus",
                          "Install the standard Ooze Edit and Window menus",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPERTIES, properties);

  object_class->get_property = ooze_application_window_get_property;
  object_class->set_property = ooze_application_window_set_property;
  object_class->constructed = ooze_application_window_constructed;
  object_class->dispose = ooze_application_window_dispose;
}

static void
ooze_application_window_init (OozeApplicationWindow *self)
{
  OozeApplicationWindowPrivate *priv;

  priv = ooze_application_window_get_instance_private (self);
  priv->standard_edit_actions = TRUE;
  priv->standard_menus = TRUE;

  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);
}

OozeApplicationWindow *
ooze_application_window_new (GtkApplication *application)
{
  OozeApplicationWindow *self;

  g_return_val_if_fail (GTK_IS_APPLICATION (application), NULL);

  self = g_object_new (OOZE_TYPE_APPLICATION_WINDOW,
                       "application", application,
                       NULL);
  return self;
}

void
ooze_application_window_set_content (OozeApplicationWindow *self,
                                     GtkWidget             *content)
{
  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));
  g_return_if_fail (content == NULL || GTK_IS_WIDGET (content));

  gtk_window_set_child (GTK_WINDOW (self), content);
}

void
ooze_application_window_set_title (OozeApplicationWindow *self,
                                   const char            *title)
{
  OozeApplicationWindowPrivate *priv;

  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));

  priv = ooze_application_window_get_instance_private (self);
  gtk_window_set_title (GTK_WINDOW (self), title);
  ooze_header_bar_set_title (priv->header, title);
}

void
ooze_application_window_set_title_widget (OozeApplicationWindow *self,
                                          GtkWidget             *widget)
{
  OozeApplicationWindowPrivate *priv;

  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));
  g_return_if_fail (widget == NULL || GTK_IS_WIDGET (widget));

  priv = ooze_application_window_get_instance_private (self);
  ooze_header_bar_set_title_widget (priv->header, widget);
}

OozeHeaderBar *
ooze_application_window_get_header_bar (OozeApplicationWindow *self)
{
  OozeApplicationWindowPrivate *priv;

  g_return_val_if_fail (OOZE_IS_APPLICATION_WINDOW (self), NULL);

  priv = ooze_application_window_get_instance_private (self);
  return priv->header;
}

void
ooze_application_window_append_menu_section (OozeApplicationWindow *self,
                                             const char            *label,
                                             GMenuModel            *section)
{
  OozeApplicationWindowPrivate *priv;

  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));
  g_return_if_fail (label != NULL);
  g_return_if_fail (G_IS_MENU_MODEL (section));

  priv = ooze_application_window_get_instance_private (self);
  g_menu_append_submenu (priv->menubar, label, section);
}

void
ooze_application_window_append_standard_edit_menu
  (OozeApplicationWindow *self)
{
  OozeApplicationWindowPrivate *priv;

  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));

  priv = ooze_application_window_get_instance_private (self);
  ooze_menubar_append_edit (priv->menubar);
}

void
ooze_application_window_append_standard_window_menu
  (OozeApplicationWindow *self)
{
  OozeApplicationWindowPrivate *priv;

  g_return_if_fail (OOZE_IS_APPLICATION_WINDOW (self));

  priv = ooze_application_window_get_instance_private (self);
  ooze_menubar_append_window (priv->menubar);
}
