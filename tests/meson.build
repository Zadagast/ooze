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
