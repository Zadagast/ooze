#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define OOZE_TYPE_APPLICATION (ooze_application_get_type ())
G_DECLARE_FINAL_TYPE (OozeApplication, ooze_application, OOZE, APPLICATION,
                      AdwApplication)

OozeApplication *ooze_application_new (const char       *application_id,
                                        GApplicationFlags flags);

G_END_DECLS
