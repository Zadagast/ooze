#pragma once

#include <clutter/clutter.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

typedef struct _MyAquaMenu MyAquaMenu;

typedef void (*MyAquaMenuActionFunc) (gpointer user_data,
                                      int      action_id);

typedef struct
{
  const char *label;
  int         action_id;
  gboolean    sensitive;
} MyAquaMenuEntry;

MyAquaMenu *my_aqua_menu_new (MetaContext           *context,
                              ClutterActor          *stage,
                              MyAquaMenuActionFunc   callback,
                              gpointer               user_data);

void my_aqua_menu_attach_anchor (MyAquaMenu  *menu,
                                 ClutterActor *anchor);

void my_aqua_menu_show_for_anchor (MyAquaMenu            *menu,
                                   ClutterActor          *anchor,
                                   const MyAquaMenuEntry *entries,
                                   gsize                  n_entries);

void my_aqua_menu_toggle_for_anchor (MyAquaMenu            *menu,
                                     ClutterActor          *anchor,
                                     const MyAquaMenuEntry *entries,
                                     gsize                  n_entries);

void my_aqua_menu_raise (MyAquaMenu *menu);

void my_aqua_menu_close (MyAquaMenu *menu);

gboolean my_aqua_menu_is_open (MyAquaMenu *menu);

gboolean my_aqua_menu_handle_key (MyAquaMenu   *menu,
                                  ClutterEvent *event);

void my_aqua_menu_destroy (MyAquaMenu *menu);

G_END_DECLS
