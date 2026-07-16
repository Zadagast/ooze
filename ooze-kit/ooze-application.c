#include "ooze-application.h"

#include "ooze-init.h"

struct _OozeApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (OozeApplication, ooze_application, ADW_TYPE_APPLICATION)

static void
ooze_application_startup (GApplication *application)
{
  G_APPLICATION_CLASS (ooze_application_parent_class)->startup (application);

  ooze_kit_init ();
}

static void
ooze_application_class_init (OozeApplicationClass *klass)
{
  G_APPLICATION_CLASS (klass)->startup = ooze_application_startup;
}

static void
ooze_application_init (OozeApplication *self G_GNUC_UNUSED)
{
}

OozeApplication *
ooze_application_new (const char       *application_id,
                      GApplicationFlags flags)
{
  return g_object_new (OOZE_TYPE_APPLICATION,
                       "application-id", application_id,
                       "flags", flags,
                       NULL);
}
