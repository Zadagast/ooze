project('my-desktop', 'c',
  version: '0.1.0',
  meson_version: '>= 1.0.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

mutter_api_version = '18'

libmutter_pc = 'libmutter-' + mutter_api_version
clutter_pc = 'mutter-clutter-' + mutter_api_version
cogl_pc = 'mutter-cogl-' + mutter_api_version
mtk_pc = 'mutter-mtk-' + mutter_api_version

cc = meson.get_compiler('c')

mutter_dep = dependency(libmutter_pc)
clutter_dep = dependency(clutter_pc)
cogl_dep = dependency(cogl_pc)
mtk_dep = dependency(mtk_pc)

c_args = [
  '-DCLUTTER_ENABLE_EXPERIMENTAL_API',
  '-DCOGL_ENABLE_EXPERIMENTAL_API',
]

gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
libpng_dep = dependency('libpng')

gtk4_dep = dependency('gtk4', required: false)
adw_dep = dependency('libadwaita-1', required: false)
cairo_dep = dependency('cairo')

inc_common  = include_directories('common')
inc_ooze_ui = include_directories('ooze-ui')
inc_ooze_kit = include_directories('ooze-kit')
inc_src     = include_directories('src')

ooze_data_dir = get_option('prefix') / 'share' / 'ooze'
c_args += '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir)
c_args += '-DOOZE_VERSION="@0@"'.format(meson.project_version())

install_icons_script = meson.project_source_root() / 'scripts' / 'install-elementary-icons.sh'

# Elementary icons are fetched only by an explicit target or install.

custom_target(
  'elementary-icons',
  output: 'elementary-icons.stamp',
  command: [find_program('bash'), install_icons_script, '@OUTPUT@'],
  build_by_default: false,
)

# The install script fetches missing icons and copies them under the install prefix.
meson.add_install_script(
  install_icons_script,
  '--install',
  'share/ooze/icons/elementary',
)

mutter_typelibdir = mutter_dep.get_variable('typelibdir')

executable('my-desktop',
  'src/main.c',
  'src/my-icons.c',
  'src/my-plugin.c',
  'src/my-aqua-draw.c',
  'src/my-dock.c',
  'src/my-desktop-icons.c',
  'src/my-aqua-menu.c',
  'src/my-global-menu.c',
  'src/my-theme.c',
  'src/my-window.c',
  'src/my-magic-lamp.c',
  'src/my-screen-transition.c',
  'common/aqua-traffic.c',
  c_args: c_args,
  include_directories: [inc_common, inc_src],
  dependencies: [
    mutter_dep,
    clutter_dep,
    cogl_dep,
    mtk_dep,
    gio_dep,
    gio_unix_dep,
    gdk_pixbuf_dep,
    libpng_dep,
    cairo_dep,
    dependency('pango'),
    dependency('pangocairo'),
    cc.find_library('m', required: true),
  ],
  build_rpath: mutter_typelibdir,
  install: true,
)

install_data('data/spot-logo.svg',
  install_dir: ooze_data_dir,
)

install_data('data/icons/Adwaita/symbolic/actions/view-column-symbolic.svg',
  install_dir: ooze_data_dir / 'icons/Adwaita/symbolic/actions',
)

install_data('data/spot-logo.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
  rename: 'org.ooze.Spot.svg',
)

vte_dep = dependency('vte-2.91-gtk4', required: false)

# ── OozeKit ─────────────────────────────────────────────────────────────────
# Shared GTK4/Cairo toolkit used by all Ooze apps.  Built as a static lib
# so each executable links it in without any shared-lib install step.
ooze_kit_sources = [
  'ooze-kit/ooze-palette.c',
  'ooze-kit/ooze-draw.c',
  'ooze-kit/ooze-surface.c',
  'ooze-kit/ooze-button.c',
  'ooze-kit/ooze-icons.c',
  'ooze-kit/ooze-toolbar.c',
  'ooze-kit/ooze-theme.c',
  'ooze-kit/ooze-about.c',
  'ooze-kit/ooze-window-actions.c',
  'ooze-kit/ooze-pinline.c',
  'ooze-kit/ooze-scroll.c',
  'ooze-kit/ooze-popover.c',
]

# Shared source list for ooze-ui widgets (header bar, traffic lights, Ooze Gel)
ooze_ui_sources = [
  'ooze-ui/ooze-traffic-lights.c',
  'ooze-ui/ooze-header-bar.c',
  'ooze-ui/ooze-gel.c',
  'common/aqua-traffic.c',
]

ooze_app_inc = [inc_common, inc_ooze_ui, inc_ooze_kit, inc_src]
ooze_app_c_args = [
  '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir),
  '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
]

if gtk4_dep.found() and adw_dep.found()
  executable('spot',
    'spot/main.c',
    'spot/spot-window.c',
    'src/my-icons.c',
    'src/my-icons-gtk.c',
    ooze_ui_sources,
    ooze_kit_sources,
    include_directories: ooze_app_inc,
    dependencies: [
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),     # libm for ooze-draw (math.h)
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Spot.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  executable('ooze-pak',
    'ooze-pak/main.c',
    'ooze-pak/ooze-pak-window.c',
    'ooze-pak/ooze-pak-flatpak.c',
    'src/my-icons.c',
    'src/my-icons-gtk.c',
    ooze_ui_sources,
    ooze_kit_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-pak')],
    dependencies: [
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Pak.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  install_data('data/pak-logo.svg',
    install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
    rename: 'org.ooze.Pak.svg',
  )

  install_data('data/pak-logo.svg',
    install_dir: ooze_data_dir,
  )

  if vte_dep.found()
    executable('ooze-command',
      'ooze-command/main.c',
      'ooze-command/ooze-command-window.c',
      'src/my-icons.c',
      'src/my-icons-gtk.c',
      ooze_ui_sources,
      ooze_kit_sources,
      include_directories: ooze_app_inc,
      dependencies: [
        gtk4_dep,
        adw_dep,
        vte_dep,
        gio_dep,
        cairo_dep,
        dependency('pango'),
        cc.find_library('m'),   # libm for ooze-draw
      ],
      c_args: ooze_app_c_args,
      install: true,
    )

    install_data('data/org.ooze.Command.desktop',
      install_dir: get_option('datadir') / 'applications',
    )
  else
    warning('Ooze Command skipped: install libvte-2.91-gtk4-dev')
  endif

  executable('ooze-king',
    'ooze-king/main.c',
    'ooze-king/ooze-king-window.c',
    'src/my-icons.c',
    'src/my-icons-gtk.c',
    ooze_ui_sources,
    ooze_kit_sources,
    include_directories: ooze_app_inc,
    dependencies: [
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.King.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  libgtop_dep = dependency('libgtop-2.0', required: false)
  udisks_dep = dependency('udisks2', required: false)
  ooze_about_c_args = ooze_app_c_args
  ooze_about_deps = [
    gtk4_dep,
    adw_dep,
    gio_dep,
    cairo_dep,
    cc.find_library('m'),
  ]
  if libgtop_dep.found()
    ooze_about_c_args += '-DHAVE_LIBGTOP=1'
    ooze_about_deps += libgtop_dep
  else
    warning('Ooze About: libgtop-2.0 not found; using /proc CPU/memory fallbacks')
  endif
  if udisks_dep.found()
    ooze_about_c_args += '-DHAVE_UDISKS2=1'
    ooze_about_deps += udisks_dep
  else
    warning('Ooze About: udisks2 not found; using root filesystem size fallback')
  endif

  executable('ooze-about',
    'ooze-about/main.c',
    'ooze-about/ooze-about-window.c',
    'ooze-about/ooze-system-info.c',
    'src/my-icons.c',
    'src/my-icons-gtk.c',
    ooze_ui_sources,
    ooze_kit_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-about')],
    dependencies: ooze_about_deps,
    c_args: ooze_about_c_args,
    install: true,
  )

  install_data('data/org.ooze.About.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  # Dev gallery for OozeKit widgets — not installed as a desktop product.
  executable('ooze-kit-factory',
    'ooze-kit-factory/main.c',
    'ooze-kit-factory/ooze-kit-factory-window.c',
    'src/my-icons.c',
    'src/my-icons-gtk.c',
    ooze_ui_sources,
    ooze_kit_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-kit-factory')],
    dependencies: [
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: false,
  )

  pipewire_dep = dependency('libpipewire-0.3', required: false)
  if pipewire_dep.found()
    ooze_sound_sources = [
      'ooze-ear/ooze-sound-pane.c',
      'ooze-ear/ooze-ear-pw.c',
    ]
    ooze_ear_inc = ooze_app_inc + [include_directories('ooze-ear')]

    executable('ooze-ear',
      'ooze-ear/main.c',
      'ooze-ear/ooze-ear-window.c',
      ooze_sound_sources,
      'src/my-icons.c',
      'src/my-icons-gtk.c',
      ooze_ui_sources,
      ooze_kit_sources,
      include_directories: ooze_ear_inc,
      dependencies: [
        gtk4_dep,
        adw_dep,
        gio_dep,
        cairo_dep,
        pipewire_dep,
        cc.find_library('m'),
      ],
      c_args: ooze_app_c_args,
      install: true,
    )

    install_data('data/org.ooze.Ear.desktop',
      install_dir: get_option('datadir') / 'applications',
    )

    install_data('data/ear-logo.svg',
      install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
      rename: 'org.ooze.Ear.svg',
    )

    install_data('data/ear-logo.svg',
      install_dir: ooze_data_dir,
    )
  else
    warning('Ooze Ear skipped: install libpipewire-0.3-dev')
  endif

else
  warning('Spot file manager skipped: install libgtk-4-dev and libadwaita-1-dev')
endif
