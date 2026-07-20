#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * OozeSegmentGroup — Cheetah-style segmented toggle for the MAIN BAR.
 *
 * One recessed track holding connected segments; exactly one segment is
 * active (pressed-in) at a time. A small caption sits under the track,
 * like the classic Finder "View" control.
 *
 *   GtkWidget *seg = ooze_segment_group_new ("View");
 *   ooze_segment_group_add (seg, icons_grid, "Grid view");
 *   ooze_segment_group_add (seg, icons_list, "List view");
 *   g_signal_connect (seg, "changed", G_CALLBACK (on_view_changed), self);
 *
 * "changed" fires with the newly active segment index.
 */

#define OOZE_TYPE_SEGMENT_GROUP (ooze_segment_group_get_type ())
G_DECLARE_FINAL_TYPE (OozeSegmentGroup, ooze_segment_group,
                      OOZE, SEGMENT_GROUP, GtkBox)

GtkWidget *ooze_segment_group_new (const char *caption);

/* Appends a segment (16px symbolic icon); returns its index. */
int ooze_segment_group_add (GtkWidget          *group,
                            const char * const *icon_names,
                            const char         *tooltip);

void ooze_segment_group_set_active (GtkWidget *group, int index);
int  ooze_segment_group_get_active (GtkWidget *group);

G_END_DECLS
