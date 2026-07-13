project('ooze', 'c', 'cpp',
  version: '0.2.1',
  meson_version: '>= 1.0.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++17',
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

inc_common      = include_directories('common')
inc_ooze_ui     = include_directories('ooze-ui')
inc_ooze_kit    = include_directories('ooze-kit')
inc_compositor  = include_directories('compositor')
inc_shared      = include_directories('shared')

ooze_data_dir = get_option('prefix') / 'share' / 'ooze'
c_args += '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir)
c_args += '-DOOZE_VERSION="@0@"'.format(meson.project_version())

install_icons_script = meson.project_source_root() / 'scripts' / 'install-elementary-icons.sh'

# Elementary icons are fetched only by an explicit target or install.
# Meson names the Ninja rule after the output file; alias so
# `ninja elementary-icons` matches scripts/README.

elementary_icons = custom_target(
  'elementary-icons',
  output: 'elementary-icons.stamp',
  command: [find_program('bash'), install_icons_script, '@OUTPUT@'],
  build_by_default: false,
)
alias_target('elementary-icons', elementary_icons)

# The install script fetches missing icons and copies them under the install prefix.
meson.add_install_script(
  install_icons_script,
  '--install',
  'share/ooze/icons/elementary',
)

mutter_typelibdir = mutter_dep.get_variable('typelibdir')

executable('ooze',
  'compositor/main.c',
  'compositor/ooze-xsettings.c',
  'compositor/ooze-plugin.c',
  'compositor/ooze-window-chrome.c',
  'compositor/ooze-panel.c',
  'compositor/ooze-shell-menu.c',
  'compositor/ooze-aqua-draw.c',
  'compositor/ooze-dock-shell.c',
  'compositor/ooze-dock-pins.c',
  'compositor/ooze-desktop-icons.c',
  'compositor/ooze-aqua-menu.c',
  'compositor/ooze-global-menu.c',
  'compositor/ooze-dbusmenu.c',
  'compositor/ooze-theme.c',
  'compositor/ooze-window.c',
  'compositor/ooze-magic-lamp.c',
  'compositor/ooze-transition.c',
  'compositor/ooze-stall.c',
  'shared/ooze-shared-icons.c',
  'shared/ooze-shared-appmenu.c',
  'shared/ooze-dnd-bridge.c',
  'shared/ooze-icon-lookup.c',
  'shared/ooze-foreign-gtk.c',
  'common/aqua-traffic.c',
  c_args: c_args,
  include_directories: [inc_common, inc_compositor, inc_shared],
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
    dependency('x11'),
    cc.find_library('m', required: true),
  ],
  build_rpath: mutter_typelibdir,
  install: true,
)

install_data('data/wayland-sessions/ooze.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)

install_data('packaging/deb/ooze-wayland-session',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data('data/xdg-desktop-portal/ooze-portals.conf',
  install_dir: get_option('datadir') / 'xdg-desktop-portal',
)

install_data(
  [
    'data/xdg-desktop-portal/portals/ooze-gtk.portal',
    'data/xdg-desktop-portal/portals/ooze-gnome.portal',
    'data/xdg-desktop-portal/portals/ooze-gnome-keyring.portal',
  ],
  install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals',
)

install_data('packaging/deb/ooze-session-env.sh',
  install_dir: get_option('datadir') / 'ooze',
  install_mode: 'rwxr-xr-x',
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

# ── OozeKit static library ──────────────────────────────────────────────────
# OozeKit + OozeUI compiled once into a static library; all GTK apps link it.
ooze_kit_lib = static_library('ooze-kit',
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
  'ooze-ui/ooze-traffic-lights.c',
  'ooze-ui/ooze-header-bar.c',
  'ooze-ui/ooze-gel.c',
  'common/aqua-traffic.c',
  include_directories: [inc_common, inc_ooze_ui, inc_ooze_kit],
  dependencies: [gtk4_dep, adw_dep, cairo_dep, cc.find_library('m')],
  c_args: [
    '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir),
    '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
  ],
)

ooze_kit_dep = declare_dependency(
  link_with: ooze_kit_lib,
  include_directories: [inc_common, inc_ooze_ui, inc_ooze_kit],
)

# Core icon/appmenu helpers used by both the compositor and GTK apps.
# ooze-shared-icons-gtk.c is GTK4-only and is NOT included in the compositor.
ooze_shared_sources = [
  'shared/ooze-shared-icons.c',
  'shared/ooze-shared-icons-gtk.c',
  'shared/ooze-shared-appmenu.c',
  'shared/ooze-dnd-bridge.c',
  'shared/ooze-icon-lookup.c',
  'shared/ooze-foreign-gtk.c',
]

ooze_app_inc = [inc_shared]
ooze_app_c_args = [
  '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir),
  '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
]

if gtk4_dep.found() and adw_dep.found()
  executable('spot',
    'spot/main.c',
    'spot/spot-window.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc,
    dependencies: [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
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
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-pak')],
    dependencies: [
      ooze_kit_dep,
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
      ooze_shared_sources,
      include_directories: ooze_app_inc,
      dependencies: [
        ooze_kit_dep,
        gtk4_dep,
        adw_dep,
        vte_dep,
        gio_dep,
        cairo_dep,
        dependency('pango'),
        cc.find_library('m'),
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
    ooze_shared_sources,
    include_directories: ooze_app_inc,
    dependencies: [
      ooze_kit_dep,
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

  ooze_monitor_inc = ooze_app_inc + [include_directories('ooze-monitor')]
  executable('ooze-monitor',
    'ooze-monitor/main.c',
    'ooze-monitor/ooze-monitor-window.c',
    'ooze-monitor/ooze-display-pane.c',
    'ooze-monitor/ooze-display-config.c',
    ooze_shared_sources,
    include_directories: ooze_monitor_inc,
    dependencies: [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Monitor.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  executable('ooze-themes',
    'ooze-themes/main.c',
    'ooze-themes/ooze-themes-window.c',
    'ooze-themes/ooze-themes-pane.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-themes')],
    dependencies: [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Themes.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  executable('ooze-eye',
    'ooze-eye/main.c',
    'ooze-eye/ooze-eye-window.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-eye')],
    dependencies: [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      gdk_pixbuf_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Eye.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  # Ooze Torrent — optional; needs ./scripts/fetch-libtransmission.sh once.
  fs = import('fs')
  tr_prefix_rel = 'third-party/libtransmission'
  tr_libdir_rel = tr_prefix_rel / 'lib'
  tr_incdir_rel = tr_prefix_rel / 'include'
  tr_sysdeps_rel = tr_prefix_rel / 'sysdeps' / 'lib'
  if fs.is_file(tr_libdir_rel / 'libtransmission.a') and fs.is_file(tr_libdir_rel / 'libdht.a')
    tr_inc = include_directories(tr_incdir_rel)
    tr_lib_abs = meson.project_source_root() / tr_libdir_rel

    tr_curl = dependency('libcurl', required: false)
    if not tr_curl.found()
      tr_curl = cc.find_library('curl', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
      if not tr_curl.found()
        tr_curl = cc.find_library('curl', required: false)
      endif
    endif
    tr_event = dependency('libevent', required: false)
    if not tr_event.found()
      tr_event = cc.find_library('event', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
      if not tr_event.found()
        tr_event = cc.find_library('event', required: false)
      endif
    endif
    tr_ssl = dependency('openssl', required: false)
    tr_deflate = dependency('libdeflate', required: false)
    if not tr_deflate.found()
      tr_deflate = cc.find_library('deflate', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
      if not tr_deflate.found()
        tr_deflate = cc.find_library('deflate', required: false)
      endif
    endif
    tr_psl = dependency('libpsl', required: false)
    tr_miniupnpc = cc.find_library('miniupnpc', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
    if not tr_miniupnpc.found()
      tr_miniupnpc = cc.find_library('miniupnpc', required: false)
    endif
    tr_natpmp = cc.find_library('natpmp', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
    if not tr_natpmp.found()
      tr_natpmp = cc.find_library('natpmp', required: false)
    endif
    tr_b64 = cc.find_library('b64', dirs: [meson.project_source_root() / tr_sysdeps_rel], required: false)
    if not tr_b64.found()
      tr_b64 = cc.find_library('b64', required: false)
    endif
    tr_z = dependency('zlib', required: false)
    if not tr_z.found()
      tr_z = cc.find_library('z', required: true)
    endif

    tr_link_args = [
      '-Wl,--start-group',
      tr_lib_abs / 'libtransmission.a',
      tr_lib_abs / 'libdht.a',
      tr_lib_abs / 'libutp.a',
      tr_lib_abs / 'libwildmat.a',
      tr_lib_abs / 'libMadlerCrcany.a',
      '-Wl,--end-group',
    ]

    tr_deps = [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      cairo_dep,
      tr_curl,
      tr_event,
      tr_ssl,
      tr_deflate,
      tr_psl,
      tr_miniupnpc,
      tr_natpmp,
      tr_b64,
      tr_z,
      cc.find_library('m'),
      meson.get_compiler('cpp').find_library('stdc++'),
    ]

    tr_deps_clean = []
    foreach d : tr_deps
      if d.found()
        tr_deps_clean += d
      endif
    endforeach

    if tr_curl.found() and tr_event.found() and tr_ssl.found() and tr_b64.found()
      executable('ooze-torrent',
        'ooze-torrent/main.c',
        'ooze-torrent/ooze-torrent-window.c',
        'ooze-torrent/ooze-torrent-session.cc',
        ooze_shared_sources,
        include_directories: ooze_app_inc + [include_directories('ooze-torrent'), tr_inc],
        dependencies: tr_deps_clean,
        link_args: tr_link_args,
        c_args: ooze_app_c_args,
        cpp_args: ooze_app_c_args,
        install: true,
      )

      install_data('data/org.ooze.Torrent.desktop',
        install_dir: get_option('datadir') / 'applications',
      )
      install_data('ooze-torrent/COPYING.GPL',
        install_dir: get_option('datadir') / 'doc' / 'ooze-torrent',
      )
    else
      warning('Ooze Torrent skipped: missing curl/event/openssl/libb64 (or sysdeps)')
    endif
  else
    warning('Ooze Torrent skipped: run ./scripts/fetch-libtransmission.sh')
  endif

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
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-about')],
    dependencies: ooze_about_deps + [ooze_kit_dep],
    c_args: ooze_about_c_args,
    install: true,
  )

  install_data('data/org.ooze.About.desktop',
    install_dir: get_option('datadir') / 'applications',
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
      ooze_shared_sources,
      include_directories: ooze_ear_inc,
      dependencies: [
        ooze_kit_dep,
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
