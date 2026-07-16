#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*OozeGridMenuActivateFunc) (gpointer user_data);

typedef struct
{
  const char * const          *icon_names;
  const char                  *label;
  gboolean                     sensitive;
  OozeGridMenuActivateFunc     activate;
  gpointer                     user_data;
  GDestroyNotify               user_data_destroy;
} OozeGridMenuItem;

/*
 * Create an arrowless popover containing a scrollable grid of OozeButtons.
 * The caller owns the popover and may parent/position it like any GtkPopover.
 */
GtkWidget *ooze_grid_menu_new (void);

/* Add a vertical icon-and-label tile to the current grid row. */
void ooze_grid_menu_append_item (GtkWidget                 *menu,
                                 const OozeGridMenuItem    *item);

/* Start a new grid section, separated from the previous one. */
void ooze_grid_menu_append_separator (GtkWidget *menu);

G_END_DECLS
