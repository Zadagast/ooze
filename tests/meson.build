test_autostart = executable('test-autostart',
  'test-autostart.c',
  '../compositor/ooze-autostart.c',
  include_directories: inc_compositor,
  dependencies: [gio_dep, gio_unix_dep],
)

test('autostart', test_autostart)

test_render_smoke = executable('test-render-smoke',
  'ooze-render-smoke.c',
  include_directories: inc_ooze_kit,
  dependencies: [ooze_kit_dep, gtk4_dep, adw_dep, cc.find_library('m')],
)

test('render-smoke', test_render_smoke,
  env: [
    'GSK_RENDERER=cairo',
    'LIBGL_ALWAYS_SOFTWARE=1',
  ],
)


test_wallpaper_smoke = executable('test-wallpaper-smoke',
  'ooze-wallpaper-smoke.c',
  '../compositor/ooze-wallpaper-source.c',
  include_directories: inc_compositor,
  dependencies: [gio_dep, gdk_pixbuf_dep, cc.find_library('m')],
)

test('wallpaper-smoke', test_wallpaper_smoke)

test_app_shared_sources = [
  '../shared/ooze-shared-appmenu.c',
  '../shared/ooze-dnd-bridge.c',
  '../shared/ooze-icon-lookup.c',
  '../shared/ooze-foreign-gtk.c',
  '../shared/ooze-color-scheme.c',
]

test_app_smoke = executable('test-app-smoke',
  'ooze-app-smoke.c',
  '../spot/spot-window.c',
  '../spot/spot-sidebar.c',
  '../spot/spot-view-grid.c',
  '../spot/spot-view-list.c',
  '../spot/spot-view-columns.c',
  '../spot/spot-fileops.c',
  '../spot/spot-actions.c',
  '../ooze-command/ooze-command-window.c',
  '../ooze-king/ooze-king-window.c',
  '../ooze-launch/ooze-launch-window.c',
  '../ooze-monitor/ooze-monitor-window.c',
  '../ooze-monitor/ooze-display-pane.c',
  '../ooze-monitor/ooze-display-config.c',
  '../ooze-pak/ooze-pak-window.c',
  '../ooze-scenery/ooze-scenery-window.c',
  '../ooze-pak/ooze-pak-flatpak.c',
  '../ooze-eye/ooze-eye-window.c',
  '../ooze-shot/ooze-shot-window.c',
  '../ooze-about/ooze-about-window.c',
  '../ooze-about/ooze-system-info.c',
  '../ooze-defaults/ooze-defaults-window.c',
  '../ooze-defaults/ooze-defaults-pane.c',
  '../ooze-themes/ooze-themes-window.c',
  '../ooze-themes/ooze-themes-pane.c',
  test_app_shared_sources,
  include_directories: [
    inc_ooze_kit,
    inc_shared,
    include_directories('../spot'),
    include_directories('../ooze-command'),
    include_directories('../ooze-king'),
    include_directories('../ooze-launch'),
    include_directories('../ooze-monitor'),
    include_directories('../ooze-pak'),
    include_directories('../ooze-eye'),
    include_directories('../ooze-shot'),
    include_directories('../ooze-about'),
    include_directories('../ooze-defaults'),
    include_directories('../ooze-themes'),
    include_directories('../ooze-scenery'),
    inc_compositor,
  ],
  dependencies: [
    ooze_kit_dep,
    gtk4_dep,
    adw_dep,
    gio_dep,
    gio_unix_dep,
    gdk_pixbuf_dep,
    vte_dep,
    dependency('pango'),
    cairo_dep,
    cc.find_library('m'),
  ],
  c_args: [
    '-DOOZE_DATA_DIR="@0@"'.format(meson.project_source_root() / 'data'),
    '-DOOZE_VERSION="@0@"'.format(meson.project_version()),
  ],
)

test_app_smoke_env = [
  'GSK_RENDERER=cairo',
  'LIBGL_ALWAYS_SOFTWARE=1',
]

test('app-smoke-spot', test_app_smoke,
  args: ['-p', '/app/spot'],
  env: test_app_smoke_env,
)
test('app-smoke-command', test_app_smoke,
  args: ['-p', '/app/command'],
  env: test_app_smoke_env,
)
test('app-smoke-king', test_app_smoke,
  args: ['-p', '/app/king'],
  env: test_app_smoke_env,
)
test('app-smoke-launch', test_app_smoke,
  args: ['-p', '/app/launch'],
  env: test_app_smoke_env,
)
test('app-smoke-eye', test_app_smoke,
  args: ['-p', '/app/eye'],
  env: test_app_smoke_env,
)
test('app-smoke-shot', test_app_smoke,
  args: ['-p', '/app/shot'],
  env: test_app_smoke_env,
)
test('app-smoke-about', test_app_smoke,
  args: ['-p', '/app/about'],
  env: test_app_smoke_env,
)
test('app-smoke-defaults', test_app_smoke,
  args: ['-p', '/app/defaults'],
  env: test_app_smoke_env,
)
test('app-smoke-monitor', test_app_smoke,
  args: ['-p', '/app/monitor'],
  env: test_app_smoke_env,
)
test('app-smoke-themes', test_app_smoke,
  args: ['-p', '/app/themes'],
  env: test_app_smoke_env,
)
test('app-smoke-pak', test_app_smoke,
  args: ['-p', '/app/pak'],
  env: test_app_smoke_env,
)
test('app-smoke-scenery', test_app_smoke,
  args: ['-p', '/app/scenery'],
  env: test_app_smoke_env,
)
