#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _OozeDbusmenu OozeDbusmenu;

typedef struct
{
  int   id;
  char *label;
  gboolean enabled;
  gboolean visible;
  gboolean separator;
  gboolean has_children;
} OozeDbusmenuItem;

OozeDbusmenu *ooze_dbusmenu_new (GDBusConnection *session,
                             const char      *bus_name,
                             const char      *object_path);

void ooze_dbusmenu_free (OozeDbusmenu *menu);

/* Root children (= panel top labels). Caller frees with ooze_dbusmenu_items_free. */
gboolean ooze_dbusmenu_get_top_items (OozeDbusmenu       *menu,
                                    OozeDbusmenuItem  **items_out,
                                    gsize            *n_out);

/* Children of a top item (one panel dropdown). */
gboolean ooze_dbusmenu_get_children (OozeDbusmenu       *menu,
                                   int               parent_id,
                                   OozeDbusmenuItem  **items_out,
                                   gsize            *n_out);

void ooze_dbusmenu_items_free (OozeDbusmenuItem *items,
                             gsize           n);

/* Send clicked / about-to-show.
 * about_to_show returns TRUE when the exporter asks for a layout refresh. */
gboolean ooze_dbusmenu_about_to_show (OozeDbusmenu *menu,
                                    int         id);
gboolean ooze_dbusmenu_click (OozeDbusmenu *menu,
                            int         id);

/* Tell the exporter a top-level menu was opened (lazy populate). */
gboolean ooze_dbusmenu_opened (OozeDbusmenu *menu,
                             int         id);

G_END_DECLS
