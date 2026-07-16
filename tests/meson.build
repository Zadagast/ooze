test_autostart = executable('test-autostart',
  'test-autostart.c',
  '../compositor/ooze-autostart.c',
  include_directories: inc_compositor,
  dependencies: [gio_dep, gio_unix_dep],
)

test('autostart', test_autostart)
