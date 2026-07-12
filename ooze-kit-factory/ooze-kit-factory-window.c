#include "ooze-kit-factory-window.h"

#include "ooze-about.h"
#include "ooze-button.h"
#include "ooze-gel.h"
#include "ooze-header-bar.h"
#include "ooze-icons.h"
#include "ooze-palette.h"
#include "ooze-pinline.h"
#include "ooze-scroll.h"
#include "ooze-surface.h"
#include "ooze-toolbar.h"
#include "ooze-traffic-lights.h"

#include <adwaita.h>

struct _OozeKitFactoryWindow
{
  GtkApplicationWindow parent_instance;

  GtkWidget *header;
  GtkWidget *sidebar;
  GtkWidget *stack;

  GtkWidget *grid_btn;
  GtkWidget *columns_btn;
  GtkWidget *toolbar_grid_btn;
  GtkWidget *toolbar_columns_btn;
  GtkWidget *title_entry;
};

G_DEFINE_FINAL_TYPE (OozeKitFactoryWindow,
                     ooze_kit_factory_window,
                     GTK_TYPE_APPLICATION_WINDOW)

typedef GtkWidget *(*FacPageBuild) (OozeKitFactoryWindow *self);

typedef struct
{
  const char  *id;
  const char  *title;
  const char  *group; /* "OozeKit" or "Ooze UI" */
  FacPageBuild build;
} FacPage;

/* ── Shared page chrome ─────────────────────────────────────────────────── */

static GtkWidget *
fac_page_header (const char *title, const char *blurb)
{
  GtkWidget *box;
  GtkWidget *t;
  GtkWidget *b;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 8);

  t = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (t), 0.0);
  gtk_widget_add_css_class (t, "ooze-emphasis");
  gtk_label_set_wrap (GTK_LABEL (t), TRUE);
  gtk_box_append (GTK_BOX (box), t);

  b = gtk_label_new (blurb);
  gtk_label_set_xalign (GTK_LABEL (b), 0.0);
  gtk_label_set_wrap (GTK_LABEL (b), TRUE);
  gtk_widget_set_opacity (b, 0.78);
  gtk_box_append (GTK_BOX (box), b);

  return box;
}

static GtkWidget *
fac_api_block (const char *const *lines)
{
  GtkWidget *box;
  gsize i;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 16);

  for (i = 0; lines[i] != NULL; i++)
    {
      GtkWidget *l = gtk_label_new (lines[i]);
      gtk_label_set_xalign (GTK_LABEL (l), 0.0);
      gtk_label_set_selectable (GTK_LABEL (l), TRUE);
      gtk_label_set_wrap (GTK_LABEL (l), TRUE);
      gtk_widget_add_css_class (l, "monospace");
      gtk_box_append (GTK_BOX (box), l);
    }

  return box;
}

static GtkWidget *
fac_demo_frame (GtkWidget *child)
{
  GtkWidget *surface;

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (surface, 12);
  gtk_widget_set_margin_end (surface, 12);
  gtk_widget_set_hexpand (surface, TRUE);
  if (child)
    gtk_box_append (GTK_BOX (surface), child);
  return surface;
}

static GtkWidget *
fac_page_shell (OozeKitFactoryWindow *self G_GNUC_UNUSED,
                const char           *title,
                const char           *blurb,
                GtkWidget            *demo,
                const char *const    *api_lines)
{
  GtkWidget *page;
  GtkWidget *scrolled;
  GtkWidget *inner;

  page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand (scrolled, TRUE);
  gtk_widget_set_vexpand (scrolled, TRUE);

  inner = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (inner), fac_page_header (title, blurb));
  if (demo)
    gtk_box_append (GTK_BOX (inner), fac_demo_frame (demo));
  if (api_lines)
    gtk_box_append (GTK_BOX (inner), fac_api_block (api_lines));

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), inner);
  gtk_box_append (GTK_BOX (page), scrolled);
  return page;
}

/* ── Icons used in demos ────────────────────────────────────────────────── */

static const char * const fac_icon_grid[] = {
  "view-grid", "view-app-grid", "view-grid-symbolic", NULL
};
static const char * const fac_icon_column[] = {
  "view-column", "view-dual", "view-column-symbolic", NULL
};
static const char * const fac_icon_back[] = {
  "go-previous", "go-previous-symbolic", NULL
};
static const char * const fac_icon_home[] = {
  "user-home", "go-home", "user-home-symbolic", NULL
};
static const char * const fac_icon_folder[] = {
  "folder", "inode-directory", NULL
};

/* ── Page: OozeButton ───────────────────────────────────────────────────── */

static void
fac_set_view_exclusive (OozeKitFactoryWindow *self, gsize active)
{
  GtkWidget *peers[2] = { self->grid_btn, self->columns_btn };
  ooze_button_set_exclusive (peers, 2, active);
}

static void
fac_set_toolbar_view_exclusive (OozeKitFactoryWindow *self, gsize active)
{
  GtkWidget *peers[2] = { self->toolbar_grid_btn, self->toolbar_columns_btn };
  ooze_button_set_exclusive (peers, 2, active);
}

static void
on_grid_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  fac_set_view_exclusive (self, 0);
}

static void
on_columns_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  fac_set_view_exclusive (self, 1);
}

static void
on_toolbar_grid_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  fac_set_toolbar_view_exclusive (self, 0);
}

static void
on_toolbar_columns_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  fac_set_toolbar_view_exclusive (self, 1);
}

static GtkWidget *
fac_page_button (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *row1;
  GtkWidget *row2;
  GtkWidget *idle;
  GtkWidget *toggled;
  GtkWidget *peers[1];
  static const char *const api[] = {
    "API",
    "  ooze_button_new_toolbar (icons, label, tooltip)",
    "  ooze_button_new_labeled (kind, icons, px, label, tip)",
    "  ooze_button_set_toggled (btn, TRUE|FALSE)",
    "  ooze_button_set_exclusive (peers, n, active_index)",
    "",
    "Glass plate hugs the icon rect only (caption stays outside).",
    "Padding is owned by measure/allocate (not CSS).",
    "Exclusive peers: exactly one shows the active plate.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (demo, 8);
  gtk_widget_set_margin_bottom (demo, 8);
  gtk_widget_set_margin_start (demo, 8);
  gtk_widget_set_margin_end (demo, 8);

  row1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  idle = ooze_button_new_toolbar (fac_icon_home, "Idle", "Normal idle tile");
  toggled = ooze_button_new_toolbar (fac_icon_column, "Toggled", "Sticky on");
  peers[0] = toggled;
  ooze_button_set_exclusive (peers, 1, 0);
  gtk_box_append (GTK_BOX (row1), idle);
  gtk_box_append (GTK_BOX (row1), toggled);
  gtk_box_append (GTK_BOX (demo), row1);

  row2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  self->grid_btn = ooze_button_new_toolbar (fac_icon_grid, "Grid", "Grid view");
  self->columns_btn = ooze_button_new_toolbar (fac_icon_column, "Columns",
                                               "Columns view");
  gtk_box_append (GTK_BOX (row2), self->grid_btn);
  gtk_box_append (GTK_BOX (row2), self->columns_btn);
  g_signal_connect (self->grid_btn, "clicked", G_CALLBACK (on_grid_clicked), self);
  g_signal_connect (self->columns_btn, "clicked",
                    G_CALLBACK (on_columns_clicked), self);
  fac_set_view_exclusive (self, 1);
  gtk_box_append (GTK_BOX (demo), row2);

  return fac_page_shell (
      self,
      "OozeButton",
      "Labeled color-icon tiles with hover, press, and sticky glass for "
      "exclusive toolbar toggles (Spot Grid / Columns).",
      demo,
      api);
}

/* ── Page: OozeToolbar ──────────────────────────────────────────────────── */

static GtkWidget *
fac_page_toolbar (OozeKitFactoryWindow *self)
{
  GtkWidget *toolbar;
  GtkWidget *nav;
  GtkWidget *view;
  GtkWidget *entry;
  static const char *const api[] = {
    "API",
    "  ooze_toolbar_new ()",
    "  ooze_toolbar_add_group (toolbar)",
    "  ooze_toolbar_add_separator (toolbar)",
    "  ooze_toolbar_add_spacer (toolbar)",
    "",
    "Always pair with ooze_button_new_toolbar() tiles.",
    "Spacing matches Adwaita .toolbar (padding/border-spacing 6px).",
    "Spacer expands so trailing widgets (Search) sit on the right.",
    "Exclusive Grid/Columns demo the sticky glass plate on MAIN BAR.",
    NULL
  };

  toolbar = ooze_toolbar_new ();
  nav = ooze_toolbar_add_group (toolbar);
  gtk_box_append (GTK_BOX (nav),
                  ooze_button_new_toolbar (fac_icon_back, "Back", "Back"));
  gtk_box_append (GTK_BOX (nav),
                  ooze_button_new_toolbar (fac_icon_home, "Home", "Home"));
  ooze_toolbar_add_separator (toolbar);
  view = ooze_toolbar_add_group (toolbar);
  self->toolbar_grid_btn = ooze_button_new_toolbar (fac_icon_grid, "Grid",
                                                    "Grid view");
  self->toolbar_columns_btn = ooze_button_new_toolbar (fac_icon_column,
                                                       "Columns",
                                                       "Columns view");
  gtk_box_append (GTK_BOX (view), self->toolbar_grid_btn);
  gtk_box_append (GTK_BOX (view), self->toolbar_columns_btn);
  g_signal_connect (self->toolbar_grid_btn, "clicked",
                    G_CALLBACK (on_toolbar_grid_clicked), self);
  g_signal_connect (self->toolbar_columns_btn, "clicked",
                    G_CALLBACK (on_toolbar_columns_clicked), self);
  fac_set_toolbar_view_exclusive (self, 0);
  ooze_toolbar_add_spacer (toolbar);
  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "Search");
  gtk_widget_set_valign (entry, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (toolbar), entry);

  return fac_page_shell (
      self,
      "OozeToolbar",
      "Aluminum toolbar surface with grouped tiles, hairline separators, "
      "icon-only glass toggles, and an expanding spacer for trailing controls.",
      toolbar,
      api);
}

/* ── Page: OozeSurface ──────────────────────────────────────────────────── */

static GtkWidget *
fac_surface_band (OozeSurfaceVariant variant, const char *caption)
{
  GtkWidget *surf;
  GtkWidget *label;

  surf = ooze_surface_new (variant, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (surf, TRUE);
  gtk_widget_set_size_request (surf, -1, 36);
  label = gtk_label_new (caption);
  gtk_widget_set_margin_start (label, 12);
  gtk_box_append (GTK_BOX (surf), label);
  return surf;
}

static GtkWidget *
fac_page_surface (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *row;
  GtkWidget *sidebar;
  static const char *const api[] = {
    "API",
    "  ooze_surface_new (OOZE_SURFACE_HEADER,   orientation)",
    "  ooze_surface_new (OOZE_SURFACE_TOOLBAR,  orientation)",
    "  ooze_surface_new (OOZE_SURFACE_SIDEBAR,  orientation)",
    "  ooze_surface_new (OOZE_SURFACE_STATUSBAR, orientation)",
    "",
    "Each variant fills from OozePalette and draws a soft edge groove.",
    "Pinlines use the Ooze Gel grid (OOZE_PIN_STRIDE 4; title/status 32,",
    "MAIN BAR 96) via ooze_stripe_origin_y() so strips share one cloth.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (demo),
                  fac_surface_band (OOZE_SURFACE_HEADER, "HEADER"));
  gtk_box_append (GTK_BOX (demo),
                  fac_surface_band (OOZE_SURFACE_TOOLBAR, "TOOLBAR"));

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request (row, -1, 72);
  sidebar = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (sidebar, 140, -1);
  gtk_widget_set_vexpand (sidebar, TRUE);
  gtk_box_append (GTK_BOX (sidebar), gtk_label_new ("SIDEBAR"));
  gtk_box_append (GTK_BOX (row), sidebar);
  gtk_box_append (GTK_BOX (row),
                  fac_surface_band (OOZE_SURFACE_TOOLBAR, "content"));
  gtk_box_append (GTK_BOX (demo), row);

  gtk_box_append (GTK_BOX (demo),
                  fac_surface_band (OOZE_SURFACE_STATUSBAR, "STATUSBAR"));

  return fac_page_shell (
      self,
      "OozeSurface",
      "Pinstriped chrome fills for header, toolbar, sidebar, and status bar. "
      "Apps compose these instead of painting CSS backgrounds.",
      demo,
      api);
}

/* ── Page: OozePinline ──────────────────────────────────────────────────── */

static GtkWidget *
fac_page_pinline (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *row;
  GtkWidget *left;
  GtkWidget *right;
  static const char *const api[] = {
    "API",
    "  ooze_pinline_new (OOZE_SIDE_RIGHT)   /* vertical */",
    "  ooze_pinline_new (OOZE_SIDE_BOTTOM)  /* horizontal */",
    "",
    "Non-interactive; paints ooze_draw_separator() from the live palette.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request (demo, -1, 120);

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_vexpand (row, TRUE);
  left = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  right = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_hexpand (left, TRUE);
  gtk_widget_set_hexpand (right, TRUE);
  gtk_widget_set_vexpand (left, TRUE);
  gtk_widget_set_vexpand (right, TRUE);
  gtk_box_append (GTK_BOX (left), gtk_label_new ("Left"));
  gtk_box_append (GTK_BOX (right), gtk_label_new ("Right"));
  gtk_box_append (GTK_BOX (row), left);
  gtk_box_append (GTK_BOX (row), ooze_pinline_new (OOZE_SIDE_RIGHT));
  gtk_box_append (GTK_BOX (row), right);
  gtk_box_append (GTK_BOX (demo), row);
  gtk_box_append (GTK_BOX (demo), ooze_pinline_new (OOZE_SIDE_BOTTOM));
  gtk_box_append (GTK_BOX (demo),
                  fac_surface_band (OOZE_SURFACE_STATUSBAR, "Below horizontal pinline"));

  return fac_page_shell (
      self,
      "OozePinline",
      "Aqua hairline separators between chrome regions — groove + highlight "
      "so stacked surfaces read as one brushed piece.",
      demo,
      api);
}

/* ── Page: OozeScroll ───────────────────────────────────────────────────── */

static GtkWidget *
fac_page_scroll (OozeKitFactoryWindow *self)
{
  GtkWidget *scrolled;
  GtkWidget *list;
  int i;
  static const char *const api[] = {
    "API",
    "  ooze_scrolled_window_new ()",
    "  ooze_scroll_ensure_css ()   /* also from ooze_theme_ensure */",
    "",
    "Overlay scrolling is off. Thumb length tracks page-size (sliding map).",
    "CSS reloads on AdwStyleManager::notify::dark.",
    NULL
  };

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled, -1, 200);
  gtk_widget_set_hexpand (scrolled, TRUE);

  list = gtk_list_box_new ();
  for (i = 1; i <= 40; i++)
    {
      g_autofree char *text = g_strdup_printf ("Document row %02d", i);
      GtkWidget *row = gtk_list_box_row_new ();
      GtkWidget *label = gtk_label_new (text);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_widget_set_margin_start (label, 10);
      gtk_widget_set_margin_top (label, 4);
      gtk_widget_set_margin_bottom (label, 4);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
    }
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);

  return fac_page_shell (
      self,
      "OozeScroll",
      "Always-visible Aqua scrollbar map — recessed trough with a raised "
      "glass thumb sized from the visible document fraction.",
      scrolled,
      api);
}

/* ── Page: OozeIcons ────────────────────────────────────────────────────── */

static GtkWidget *
fac_icon_cell (const char *const *names, int px, const char *caption)
{
  GtkWidget *box;
  GtkWidget *img;
  GtkWidget *label;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
  img = ooze_icon_image_new (names, px);
  label = gtk_label_new (caption);
  gtk_box_append (GTK_BOX (box), img);
  gtk_box_append (GTK_BOX (box), label);
  return box;
}

static GtkWidget *
fac_page_icons (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  static const char *const api[] = {
    "API",
    "  OOZE_ICON_SIZE_TOOLBAR  40",
    "  OOZE_ICON_SIZE_SIDEBAR  24",
    "  OOZE_ICON_SIZE_LIST     16",
    "  OOZE_ICON_SIZE_GRID     48",
    "  ooze_icon_image_new (icon_names, icon_px)",
    "",
    "Prefers full-color theme icons; falls back to symbolic only if needed.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 24);
  gtk_widget_set_halign (demo, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (demo, 16);
  gtk_widget_set_margin_bottom (demo, 16);
  gtk_box_append (GTK_BOX (demo),
                  fac_icon_cell (fac_icon_folder, OOZE_ICON_SIZE_LIST, "LIST 16"));
  gtk_box_append (GTK_BOX (demo),
                  fac_icon_cell (fac_icon_folder, OOZE_ICON_SIZE_TOOLBAR, "TOOLBAR 40"));
  gtk_box_append (GTK_BOX (demo),
                  fac_icon_cell (fac_icon_folder, OOZE_ICON_SIZE_GRID, "GRID 48"));

  return fac_page_shell (
      self,
      "OozeIcons",
      "Canonical pixel sizes and color-first theme lookup used by Spot tiles "
      "and every labeled OozeButton.",
      demo,
      api);
}

/* ── Page: OozeAbout ────────────────────────────────────────────────────── */

static void
on_about_clicked (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  ooze_about_present (GTK_WINDOW (self),
                      "OozeKit Factory",
                      "applications-engineering",
                      "Catalog of OozeKit and Ooze UI widgets.",
                      OOZE_VERSION);
}

static GtkWidget *
fac_page_about (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *btn;
  static const char *const api[] = {
    "API",
    "  ooze_about_present (parent, brand_name, icon_name, comments, version)",
    "",
    "Opens a small Ooze Gel About window for that app's brand name.",
    "Friendly launcher names stay elsewhere.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign (demo, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top (demo, 16);
  gtk_widget_set_margin_bottom (demo, 16);
  btn = ooze_button_new_toolbar (fac_icon_home, "About", "Show About");
  g_signal_connect (btn, "clicked", G_CALLBACK (on_about_clicked), self);
  gtk_box_append (GTK_BOX (demo), btn);

  return fac_page_shell (
      self,
      "OozeAbout",
      "Shared About window so Help → About opens an Ooze Gel window for "
      "that app’s brand — not the system About This Computer app.",
      demo,
      api);
}

/* ── Page: Palette / Theme ──────────────────────────────────────────────── */

typedef struct
{
  double r, g, b;
  char   label[32];
} FacSwatch;

static void
fac_swatch_draw (GtkDrawingArea *area G_GNUC_UNUSED,
                 cairo_t        *cr,
                 int             width,
                 int             height,
                 gpointer        user_data)
{
  FacSwatch *s = user_data;

  cairo_set_source_rgb (cr, s->r, s->g, s->b);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_fill (cr);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
  cairo_rectangle (cr, 0.5, 0.5, width - 1, height - 1);
  cairo_stroke (cr);
}

static GtkWidget *
fac_swatch_widget (const char *name, double r, double g, double b)
{
  GtkWidget *box;
  GtkWidget *area;
  GtkWidget *label;
  FacSwatch *s;

  s = g_new0 (FacSwatch, 1);
  s->r = r;
  s->g = g;
  s->b = b;
  g_snprintf (s->label, sizeof s->label, "%s", name);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (area, 72, 40);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (area),
                                  fac_swatch_draw, s, g_free);
  label = gtk_label_new (name);
  gtk_box_append (GTK_BOX (box), area);
  gtk_box_append (GTK_BOX (box), label);
  return box;
}

static void
fac_rebuild_swatches (GtkWidget *row)
{
  const OozePalette *pal;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (row)) != NULL)
    gtk_box_remove (GTK_BOX (row), child);

  pal = ooze_palette_current ();
  gtk_box_append (GTK_BOX (row),
                  fac_swatch_widget ("header", pal->header_r, pal->header_g, pal->header_b));
  gtk_box_append (GTK_BOX (row),
                  fac_swatch_widget ("toolbar", pal->toolbar_r, pal->toolbar_g, pal->toolbar_b));
  gtk_box_append (GTK_BOX (row),
                  fac_swatch_widget ("sidebar", pal->sidebar_r, pal->sidebar_g, pal->sidebar_b));
  gtk_box_append (GTK_BOX (row),
                  fac_swatch_widget ("status", pal->statusbar_r, pal->statusbar_g, pal->statusbar_b));
  gtk_box_append (GTK_BOX (row),
                  fac_swatch_widget ("accent", pal->accent_r, pal->accent_g, pal->accent_b));
}

static void
on_palette_dark_changed (GObject    *obj G_GNUC_UNUSED,
                         GParamSpec *pspec G_GNUC_UNUSED,
                         GtkWidget  *row)
{
  fac_rebuild_swatches (row);
}

static GtkWidget *
fac_page_palette (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *row;
  GtkWidget *hint;
  static const char *const api[] = {
    "API",
    "  const OozePalette *ooze_palette_current (void)",
    "  ooze_theme_ensure ()   /* Inter UI font + scroll CSS */",
    "",
    "Internals: ooze_draw_surface / ooze_draw_separator / ooze_draw_button_bg",
    "take a palette pointer — one lookup per snapshot pass.",
    "Toggle system light/dark to refresh these swatches.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (demo, 12);
  gtk_widget_set_margin_bottom (demo, 12);
  gtk_widget_set_margin_start (demo, 8);
  gtk_widget_set_margin_end (demo, 8);

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_halign (row, GTK_ALIGN_CENTER);
  fac_rebuild_swatches (row);
  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (on_palette_dark_changed), row, 0);
  gtk_box_append (GTK_BOX (demo), row);

  hint = gtk_label_new (ooze_palette_current ()->dark
                          ? "Current: dark palette"
                          : "Current: light palette");
  gtk_widget_set_halign (hint, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (demo), hint);

  return fac_page_shell (
      self,
      "Palette / Theme",
      "Live OozePalette colors and theme bootstrap. Surfaces, pinlines, and "
      "glass plates all read from this table.",
      demo,
      api);
}

/* ── Page: OozeHeaderBar ────────────────────────────────────────────────── */

static void
on_title_apply (GtkButton *btn G_GNUC_UNUSED, OozeKitFactoryWindow *self)
{
  const char *text;

  if (!self->title_entry || !self->header)
    return;
  text = gtk_editable_get_text (GTK_EDITABLE (self->title_entry));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header),
                             (text && text[0]) ? text : "OozeKit Factory");
}

static GtkWidget *
fac_page_headerbar (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *row;
  GtkWidget *apply;
  static const char *const api[] = {
    "API",
    "  ooze_header_bar_new ()",
    "  ooze_header_bar_attach_window (bar, window)",
    "  ooze_header_bar_set_title (bar, title)",
    "",
    "This window’s titlebar is a live OozeHeaderBar (traffic lights +",
    "centered title). Edit the field below to try set_title.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (demo, 16);
  gtk_widget_set_margin_bottom (demo, 16);
  gtk_widget_set_margin_start (demo, 12);
  gtk_widget_set_margin_end (demo, 12);

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  self->title_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->title_entry), "Window title");
  gtk_editable_set_text (GTK_EDITABLE (self->title_entry), "OozeKit Factory");
  gtk_widget_set_hexpand (self->title_entry, TRUE);
  apply = ooze_button_new_toolbar (fac_icon_home, "Apply", "Apply title");
  g_signal_connect (apply, "clicked", G_CALLBACK (on_title_apply), self);
  gtk_box_append (GTK_BOX (row), self->title_entry);
  gtk_box_append (GTK_BOX (row), apply);
  gtk_box_append (GTK_BOX (demo), row);

  return fac_page_shell (
      self,
      "OozeHeaderBar",
      "CSD titlebar: pinstripe aluminum, traffic lights on the left, title "
      "centered on the full bar (Aqua).",
      demo,
      api);
}

/* ── Page: OozeTrafficLights ────────────────────────────────────────────── */

static GtkWidget *
fac_page_traffic (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *lights;
  GtkWidget *note;
  static const char *const api[] = {
    "API",
    "  ooze_traffic_lights_new ()",
    "  ooze_traffic_lights_attach_window (lights, window)",
    "",
    "Hit-test close / minimize / zoom. Normally embedded in OozeHeaderBar.",
    "Sample below is attached to this window — clicks control it.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (demo, 16);
  gtk_widget_set_margin_bottom (demo, 16);
  gtk_widget_set_halign (demo, GTK_ALIGN_CENTER);

  lights = GTK_WIDGET (ooze_traffic_lights_new ());
  ooze_traffic_lights_attach_window (OOZE_TRAFFIC_LIGHTS (lights),
                                     GTK_WINDOW (self));
  gtk_box_append (GTK_BOX (demo), lights);

  note = gtk_label_new ("Close / minimize / zoom (attached to this window)");
  gtk_widget_set_opacity (note, 0.75);
  gtk_box_append (GTK_BOX (demo), note);

  return fac_page_shell (
      self,
      "OozeTrafficLights",
      "Classic red / yellow / green window controls sized for the Ooze Gel "
      "title strip (AQUA_TITLEBAR_HEIGHT on the pinline grid).",
      demo,
      api);
}

/* ── Page: OozeGel ──────────────────────────────────────────────────────── */

static GtkWidget *
fac_page_gel (OozeKitFactoryWindow *self)
{
  GtkWidget *demo;
  GtkWidget *label;
  static const char *const api[] = {
    "API",
    "  ooze_gel_install_edge_resize (window)",
    "  ooze_gel_install_drag (widget, window)",
    "  ooze_gel_install_resize_handles (window, root)  /* optional full wrap */",
    "",
    "This Factory window already has edge resize via header attach.",
    "Drag the titlebar to move; pull window edges to resize.",
    NULL
  };

  demo = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top (demo, 20);
  gtk_widget_set_margin_bottom (demo, 20);
  gtk_widget_set_halign (demo, GTK_ALIGN_CENTER);
  label = gtk_label_new (
      "Ooze Gel is the window-frame layer (title bar, traffic lights,\n"
      "drag / resize). It shares the Ooze Gel pinline grid with OozeKit\n"
      "surfaces: stride 4, title/status 32, MAIN BAR 96.");
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_box_append (GTK_BOX (demo), label);

  return fac_page_shell (
      self,
      "OozeGel",
      "Ooze Gel frame behavior plus the shared pinline grid "
      "(OOZE_PIN_STRIDE / title 32 / MAIN BAR 96).",
      demo,
      api);
}

/* ── Catalog table ──────────────────────────────────────────────────────── */

static const FacPage fac_pages[] = {
  { "button",   "OozeButton",        "OozeKit", fac_page_button },
  { "toolbar",  "OozeToolbar",       "OozeKit", fac_page_toolbar },
  { "surface",  "OozeSurface",       "OozeKit", fac_page_surface },
  { "pinline",  "OozePinline",       "OozeKit", fac_page_pinline },
  { "scroll",   "OozeScroll",        "OozeKit", fac_page_scroll },
  { "icons",    "OozeIcons",         "OozeKit", fac_page_icons },
  { "about",    "OozeAbout",         "OozeKit", fac_page_about },
  { "palette",  "Palette / Theme",   "OozeKit", fac_page_palette },
  { "header",   "OozeHeaderBar",     "Ooze UI", fac_page_headerbar },
  { "traffic",  "OozeTrafficLights", "Ooze UI", fac_page_traffic },
  { "gel",      "OozeGel",           "Ooze UI", fac_page_gel },
};

static void
on_sidebar_row_selected (GtkListBox            *box G_GNUC_UNUSED,
                         GtkListBoxRow         *row,
                         OozeKitFactoryWindow  *self)
{
  const char *id;

  if (!row || !self->stack)
    return;
  id = g_object_get_data (G_OBJECT (row), "fac-page-id");
  if (id)
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), id);
}

static GtkWidget *
fac_build_sidebar (OozeKitFactoryWindow *self)
{
  GtkWidget *sidebar;
  GtkWidget *list;
  const char *last_group = NULL;
  gsize i;

  sidebar = ooze_surface_new (OOZE_SURFACE_SIDEBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request (sidebar, 200, -1);
  gtk_widget_set_vexpand (sidebar, TRUE);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  gtk_widget_set_vexpand (list, TRUE);
  self->sidebar = list;

  for (i = 0; i < G_N_ELEMENTS (fac_pages); i++)
    {
      GtkWidget *row;
      GtkWidget *label;

      if (!last_group || g_strcmp0 (last_group, fac_pages[i].group) != 0)
        {
          GtkWidget *head_row = gtk_list_box_row_new ();
          GtkWidget *head = gtk_label_new (fac_pages[i].group);

          gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (head_row), FALSE);
          gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (head_row), FALSE);
          gtk_label_set_xalign (GTK_LABEL (head), 0.0);
          gtk_widget_add_css_class (head, "ooze-emphasis");
          gtk_widget_set_margin_start (head, 10);
          gtk_widget_set_margin_top (head, 10);
          gtk_widget_set_margin_bottom (head, 4);
          gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (head_row), head);
          gtk_list_box_append (GTK_LIST_BOX (list), head_row);
          last_group = fac_pages[i].group;
        }

      row = gtk_list_box_row_new ();
      label = gtk_label_new (fac_pages[i].title);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_widget_set_margin_start (label, 14);
      gtk_widget_set_margin_end (label, 8);
      gtk_widget_set_margin_top (label, 4);
      gtk_widget_set_margin_bottom (label, 4);
      gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
      g_object_set_data (G_OBJECT (row), "fac-page-id",
                         (gpointer) fac_pages[i].id);
      gtk_list_box_append (GTK_LIST_BOX (list), row);
    }

  g_signal_connect (list, "row-selected",
                    G_CALLBACK (on_sidebar_row_selected), self);

  gtk_box_append (GTK_BOX (sidebar), list);
  return sidebar;
}

/* ── Window ─────────────────────────────────────────────────────────────── */

static void
ooze_kit_factory_window_class_init (OozeKitFactoryWindowClass *klass G_GNUC_UNUSED)
{
}

static void
ooze_kit_factory_window_init (OozeKitFactoryWindow *self)
{
  GtkWidget *shell;
  GtkWidget *paned;
  GtkWidget *sidebar;
  gsize i;

  ooze_toolbar_ensure_css ();

  gtk_window_set_default_size (GTK_WINDOW (self), 900, 640);
  gtk_window_set_title (GTK_WINDOW (self), "OozeKit Factory");
  gtk_window_set_icon_name (GTK_WINDOW (self), "applications-engineering");
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-kit-factory");

  self->header = GTK_WIDGET (ooze_header_bar_new ());
  ooze_header_bar_attach_window (OOZE_HEADER_BAR (self->header), GTK_WINDOW (self));
  ooze_header_bar_set_title (OOZE_HEADER_BAR (self->header), "OozeKit Factory");
  gtk_window_set_titlebar (GTK_WINDOW (self), self->header);

  shell = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand (paned, TRUE);
  gtk_widget_set_vexpand (paned, TRUE);
  gtk_paned_set_shrink_start_child (GTK_PANED (paned), FALSE);
  gtk_paned_set_resize_start_child (GTK_PANED (paned), FALSE);

  sidebar = fac_build_sidebar (self);
  gtk_paned_set_start_child (GTK_PANED (paned), sidebar);

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);

  for (i = 0; i < G_N_ELEMENTS (fac_pages); i++)
    {
      GtkWidget *page = fac_pages[i].build (self);
      gtk_stack_add_named (GTK_STACK (self->stack), page, fac_pages[i].id);
    }

  gtk_paned_set_end_child (GTK_PANED (paned), self->stack);
  gtk_box_append (GTK_BOX (shell), paned);
  gtk_window_set_child (GTK_WINDOW (self), shell);

  /* Select first catalog entry (skip group header at index 0). */
  {
    GtkListBoxRow *first = gtk_list_box_get_row_at_index (
        GTK_LIST_BOX (self->sidebar), 1);
    if (first)
      gtk_list_box_select_row (GTK_LIST_BOX (self->sidebar), first);
  }
}

GtkWidget *
ooze_kit_factory_window_new (GtkApplication *app)
{
  return g_object_new (OOZE_TYPE_KIT_FACTORY_WINDOW,
                       "application", app,
                       NULL);
}
