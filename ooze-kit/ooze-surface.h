#pragma once

#include <gtk/gtk.h>

/*
 * OozeSurface — GtkBox subclass that paints the standard Ooze chrome finish.
 *
 * Each variant has a designated surface colour from the OozePalette and a
 * canonical separator edge.  Pinstripes are applied automatically.
 *
 *   OOZE_SURFACE_HEADER    – title-bar fill   + bottom separator
 *   OOZE_SURFACE_TOOLBAR   – toolbar fill      + bottom separator
 *   OOZE_SURFACE_SIDEBAR   – sidebar fill      + right  separator
 *   OOZE_SURFACE_STATUSBAR – status-bar fill   + top    separator
 *
 * Drop-in replacement for both the old SpotAluminumBin in spot-window.c and
 * the gradient logic in ooze-header-bar.c's snapshot override.
 */
typedef enum
{
  OOZE_SURFACE_HEADER,
  OOZE_SURFACE_TOOLBAR,
  OOZE_SURFACE_SIDEBAR,
  OOZE_SURFACE_STATUSBAR,
} OozeSurfaceVariant;

#define OOZE_TYPE_SURFACE (ooze_surface_get_type ())
G_DECLARE_FINAL_TYPE (OozeSurface, ooze_surface, OOZE, SURFACE, GtkBox)

/*
 * Create a new OozeSurface.
 * `orientation` controls the GtkBox layout direction for children.
 *
 * STATUSBAR surfaces get corner-safe padding so labels clear the CSD
 * window radius (see OOZE_CHROME_CORNER_INSET).
 */
GtkWidget *ooze_surface_new (OozeSurfaceVariant variant,
                             GtkOrientation     orientation);

/* Match window.csd decoration border-radius so chrome clears the curve. */
#define OOZE_CHROME_CORNER_INSET 10

