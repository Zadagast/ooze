project('ooze', 'c', 'cpp',
  version: '0.4.0',
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
fs = import('fs')
gnome = import('gnome')
pkgconfig = import('pkgconfig')

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

wayland_server_dep = dependency('wayland-server')
wayland_scanner = find_program('wayland-scanner')

# org_kde_kwin_appmenu (vendored from plasma-wayland-protocols).
appmenu_protocol_xml = files('protocols/appmenu.xml')
appmenu_server_header = custom_target('appmenu-server-header',
  input: appmenu_protocol_xml,
  output: 'appmenu-server-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)
appmenu_protocol_code = custom_target('appmenu-protocol-code',
  input: appmenu_protocol_xml,
  output: 'appmenu-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

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
  'compositor/ooze-portal-env.c',
  'compositor/ooze-autostart.c',
  'compositor/ooze-polkit.c',
  'compositor/ooze-session-dialog.c',
  'compositor/ooze-foreign-gel.c',
  'compositor/ooze-aqua-draw.c',
  'compositor/ooze-dock-shell.c',
  'compositor/ooze-window-tracker.c',
  'compositor/ooze-dock-pins.c',
  'compositor/ooze-dock-folder-popup.c',
  'compositor/ooze-desktop-icons.c',
  'compositor/ooze-aqua-menu.c',
  'compositor/ooze-global-menu.c',
  'compositor/ooze-appmenu-wayland.c',
  appmenu_server_header,
  appmenu_protocol_code,
  'compositor/ooze-dbusmenu.c',
  'compositor/ooze-theme.c',
  'compositor/ooze-window.c',
  'compositor/ooze-magic-lamp.c',
  'compositor/ooze-transition.c',
  'compositor/ooze-stall.c',
  'compositor/ooze-lock.c',
  'compositor/ooze-screensaver.c',
  'compositor/ooze-flow.c',
  'compositor/ooze-flow-gpu.c',
  'compositor/ooze-wallpaper.c',
  'compositor/ooze-wallpaper-source.c',
  'compositor/ooze-sni.c',
  'compositor/ooze-tray.c',
  'compositor/ooze-notifications.c',
  'compositor/ooze-shot.c',
  'shared/ooze-shared-icons.c',
  'shared/ooze-shared-appmenu.c',
  'shared/ooze-dnd-bridge.c',
  'shared/ooze-icon-lookup.c',
  'shared/ooze-foreign-gtk.c',
  'shared/ooze-color-scheme.c',
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
    dependency('polkit-agent-1'),
    dependency('polkit-gobject-1'),
    wayland_server_dep,
    cc.find_library('m', required: true),
  ],
  build_rpath: mutter_typelibdir,
  install_rpath: mutter_typelibdir,
  install: true,
)

# PAM runs out-of-process so the compositor never blocks on pam_authenticate.
# Prefer -lpam (needs libpam0g-dev); otherwise link the SONAME directly so
# builds still work on hosts that only ship the runtime .so.0.
pam_lib = cc.find_library('pam', required: false)
if not pam_lib.found()
  pam_so = [
    '/usr/lib/x86_64-linux-gnu/libpam.so.0',
    '/usr/lib64/libpam.so.0',
    '/lib/x86_64-linux-gnu/libpam.so.0',
  ]
  pam_link = []
  foreach candidate : pam_so
    if fs.exists(candidate)
      pam_link = [candidate]
      break
    endif
  endforeach
  if pam_link.length() == 0
    error('libpam not found (install libpam0g-dev or libpam runtime)')
  endif
  pam_lib = declare_dependency(link_args: pam_link)
endif
executable('ooze-pam-helper',
  'compositor/ooze-pam-helper.c',
  include_directories: [inc_compositor],
  dependencies: [pam_lib],
  install: true,
)

install_data('data/pam/ooze-lock',
  install_dir: get_option('sysconfdir') / 'pam.d',
  rename: 'ooze-lock',
)

schema_files = files('data/org.ooze.Scenery.gschema.xml')
glib_compile_schemas = find_program('glib-compile-schemas')
compiled_schemas = custom_target('gschemas.compiled',
  input: schema_files,
  output: 'gschemas.compiled',
  command: [
    glib_compile_schemas,
    '--strict',
    '--targetdir', '@OUTDIR@',
    meson.project_source_root() / 'data',
  ],
  build_by_default: true,
  install: false,
)
install_data(schema_files,
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas',
)

install_data('data/wayland-sessions/ooze.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)

install_data('packaging/deb/ooze-wayland-session',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data('scripts/ooze-doctor',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data('scripts/ooze-update',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data('scripts/ooze-dev',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data('data/xdg-desktop-portal/ooze-portals.conf',
  install_dir: get_option('datadir') / 'xdg-desktop-portal',
)

install_data(
  [
    'data/xdg-desktop-portal/portals/ooze-gtk.portal',
    'data/xdg-desktop-portal/portals/ooze-gnome-keyring.portal',
    'data/xdg-desktop-portal/portals/ooze.portal',
  ],
  install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals',
)

install_data('packaging/deb/ooze-session-env.sh',
  install_dir: get_option('datadir') / 'ooze',
  install_mode: 'rwxr-xr-x',
)

install_data('data/icons/Adwaita/symbolic/actions/view-column-symbolic.svg',
  install_dir: ooze_data_dir / 'icons/Adwaita/symbolic/actions',
)

install_data('data/icons/hicolor/scalable/apps/org.ooze.Launch.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)
install_data('data/icons/hicolor/scalable/apps/org.ooze.Launch.svg',
  install_dir: ooze_data_dir / 'icons/hicolor/scalable/apps',
)
install_data('data/icons/hicolor/scalable/apps/org.ooze.Scenery.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)
install_data('data/icons/hicolor/scalable/apps/org.ooze.Scenery.svg',
  install_dir: ooze_data_dir / 'icons/hicolor/scalable/apps',
)

vte_dep = dependency('vte-2.91-gtk4', required: false)

# ── OozeKit shared library ──────────────────────────────────────────────────
# OozeKit + OozeUI and the icon helpers they use are compiled once into a
# versioned shared library; all GTK apps link it.
ooze_kit_lib = shared_library('ooze-kit',
  'ooze-kit/ooze-application.c',
  'ooze-kit/ooze-init.c',
  'ooze-kit/ooze-application-window.c',
  'ooze-kit/ooze-palette.c',
  'ooze-kit/ooze-draw.c',
  'ooze-kit/ooze-surface.c',
  'ooze-kit/ooze-button.c',
  'ooze-kit/ooze-icons.c',
  'ooze-kit/ooze-toolbar.c',
  'ooze-kit/ooze-theme.c',
  'ooze-kit/ooze-about.c',
  'ooze-kit/ooze-feedback.c',
  'ooze-kit/ooze-action-bar.c',
  'ooze-kit/ooze-window-actions.c',
  'ooze-kit/ooze-pinline.c',
  'ooze-kit/ooze-scroll.c',
  'ooze-kit/ooze-popover.c',
  'ooze-kit/ooze-grid-menu.c',
  'ooze-kit/ooze-segment.c',
  'ooze-ui/ooze-traffic-lights.c',
  'ooze-ui/ooze-header-bar.c',
  'ooze-ui/ooze-gel.c',
  'common/aqua-traffic.c',
  'shared/ooze-shared-icons.c',
  'shared/ooze-shared-icons-gtk.c',
  include_directories: [inc_common, inc_ooze_ui, inc_ooze_kit, inc_shared],
  dependencies: [
    gtk4_dep,
    adw_dep,
    gio_dep,
    cairo_dep,
    cc.find_library('m'),
  ],
  version: meson.project_version(),
  soversion: '0',
  install: true,
  install_dir: get_option('libdir'),
  c_args: [
    '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir),
    '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
  ],
)

ooze_kit_dep = declare_dependency(
  link_with: ooze_kit_lib,
  include_directories: [inc_common, inc_ooze_ui, inc_ooze_kit],
  compile_args: ['-DOOZEKIT_BUNDLED=1'],
)

meson.override_dependency('oozekit', ooze_kit_dep)

oozekit_public_headers = [
  'ooze-kit/ooze-application.h',
  'ooze-kit/ooze-init.h',
  'ooze-kit/ooze-application-window.h',
  'ooze-kit/ooze-about.h',
  'ooze-kit/ooze-feedback.h',
  'ooze-kit/ooze-action-bar.h',
  'ooze-kit/ooze-button.h',
  'ooze-kit/ooze-draw.h',
  'ooze-kit/ooze-icons.h',
  'ooze-kit/ooze-palette.h',
  'ooze-kit/ooze-pinline.h',
  'ooze-kit/ooze-popover.h',
  'ooze-kit/ooze-scroll.h',
  'ooze-kit/ooze-grid-menu.h',
  'ooze-kit/ooze-segment.h',
  'ooze-kit/ooze-surface.h',
  'ooze-kit/ooze-theme.h',
  'ooze-kit/ooze-toolbar.h',
  'ooze-kit/ooze-window-actions.h',
  'ooze-ui/ooze-gel.h',
  'ooze-ui/ooze-header-bar.h',
  'ooze-ui/ooze-traffic-lights.h',
]

install_headers('ooze-kit/oozekit.h')
install_headers(oozekit_public_headers, subdir: 'oozekit')

pkgconfig.generate(
  ooze_kit_lib,
  name: 'libooze-kit',
  filebase: 'oozekit',
  description: 'OozeKit SDK',
  version: meson.project_version(),
  subdirs: ['.', 'oozekit'],
  requires: [gtk4_dep, adw_dep],
)

subdir('examples')
subdir('tests')

# Shared helpers used by the compositor and GTK apps, excluding the icon
# helpers now folded into OozeKit above. The GTK4 icon helper is not included
# in the compositor.
ooze_shared_sources = [
  'shared/ooze-shared-appmenu.c',
  'shared/ooze-dnd-bridge.c',
  'shared/ooze-icon-lookup.c',
  'shared/ooze-foreign-gtk.c',
  'shared/ooze-color-scheme.c',
]

ooze_app_inc = [inc_shared]
ooze_app_c_args = [
  '-DOOZE_DATA_DIR="@0@"'.format(ooze_data_dir),
  '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
]

if gtk4_dep.found() and adw_dep.found()
  portal_dbus_sources = gnome.gdbus_codegen(
    'portal-dbus-generated',
    sources: 'portal/org.freedesktop.impl.portal.xml',
    namespace: 'OozePortal',
    interface_prefix: 'org.freedesktop.impl.portal.',
  )
  mutter_screencast_sources = gnome.gdbus_codegen(
    'mutter-screencast-generated',
    sources: 'portal/org.gnome.Mutter.ScreenCast.xml',
    namespace: 'OozeMutter',
  )

  executable('ooze-portal',
    'portal/main.c',
    'portal/portal-backend.c',
    'ooze-monitor/ooze-display-config.c',
    portal_dbus_sources,
    mutter_screencast_sources,
    ooze_shared_sources,
    include_directories: ooze_app_inc + [
      include_directories('portal'),
      include_directories('ooze-monitor'),
    ],
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
    install_dir: get_option('libexecdir'),
  )

  ooze_portal_service = configure_file(
    input: 'data/org.freedesktop.impl.portal.desktop.ooze.service',
    output: 'org.freedesktop.impl.portal.desktop.ooze.service',
    configuration: {
      'libexecdir': join_paths(get_option('prefix'), get_option('libexecdir')),
    },
  )
  install_data(ooze_portal_service,
    install_dir: get_option('datadir') / 'dbus-1' / 'services',
  )
  install_data('data/systemd/user/xdg-desktop-portal-ooze.service',
    install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user',
  )

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
  install_data(configure_file(
    input: 'data/org.freedesktop.FileManager1.service',
    output: 'org.freedesktop.FileManager1.service',
    configuration: {
      'bindir': join_paths(get_option('prefix'), get_option('bindir')),
    },
  ), install_dir: get_option('datadir') / 'dbus-1' / 'services')

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

  executable('ooze-launch',
    'ooze-launch/main.c',
    'ooze-launch/ooze-launch-window.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc,
    dependencies: [
      ooze_kit_dep,
      gtk4_dep,
      adw_dep,
      gio_dep,
      gio_unix_dep,
      cairo_dep,
      cc.find_library('m'),
    ],
    c_args: ooze_app_c_args,
    install: true,
  )

  install_data('data/org.ooze.Launch.desktop',
    install_dir: get_option('datadir') / 'applications',
  )

  executable('ooze-scenery',
    'ooze-scenery/main.c',
    'ooze-scenery/ooze-scenery-window.c',
    'compositor/ooze-flow.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-scenery'),
                                         inc_compositor],
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

  install_data('data/org.ooze.Scenery.desktop',
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

  executable('ooze-defaults',
    'ooze-defaults/main.c',
    'ooze-defaults/ooze-defaults-window.c',
    'ooze-defaults/ooze-defaults-pane.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-defaults')],
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

  install_data('data/org.ooze.Defaults.desktop',
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

  executable('ooze-shot',
    'ooze-shot/main.c',
    'ooze-shot/ooze-shot-window.c',
    ooze_shared_sources,
    include_directories: ooze_app_inc + [include_directories('ooze-shot')],
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

  install_data('data/org.ooze.Shot.desktop',
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
  else
    warning('Ooze Ear skipped: install libpipewire-0.3-dev')
  endif

else
  warning('Spot file manager skipped: install libgtk-4-dev and libadwaita-1-dev')
endif
