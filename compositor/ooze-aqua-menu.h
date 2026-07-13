#pragma once

#include <clutter/clutter.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

typedef struct _OozeAquaMenu OozeAquaMenu;

typedef void (*OozeAquaMenuActionFunc) (gpointer user_data,
                                      int      action_id);

typedef struct
{
  const char *label;
  int         action_id;
  gboolean    sensitive;
} OozeAquaMenuEntry;

OozeAquaMenu *ooze_aqua_menu_new (MetaContext           *context,
                              ClutterActor          *stage,
                              OozeAquaMenuActionFunc   callback,
                              gpointer               user_data);

void ooze_aqua_menu_attach_anchor (OozeAquaMenu  *menu,
                                 ClutterActor *anchor);

void ooze_aqua_menu_show_for_anchor (OozeAquaMenu            *menu,
                                   ClutterActor          *anchor,
                                   const OozeAquaMenuEntry *entries,
                                   gsize                  n_entries);

void ooze_aqua_menu_toggle_for_anchor (OozeAquaMenu            *menu,
                                     ClutterActor          *anchor,
                                     const OozeAquaMenuEntry *entries,
                                     gsize                  n_entries);

void ooze_aqua_menu_raise (OozeAquaMenu *menu);

void ooze_aqua_menu_close (OozeAquaMenu *menu);

gboolean ooze_aqua_menu_is_open (OozeAquaMenu *menu);

gboolean ooze_aqua_menu_handle_key (OozeAquaMenu   *menu,
                                  ClutterEvent *event);

void ooze_aqua_menu_destroy (OozeAquaMenu *menu);

G_END_DECLS
