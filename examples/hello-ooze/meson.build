project('hello-ooze', 'c',
  version: '0.1.0',
  meson_version: '>= 1.0.0',
  default_options: ['c_std=c11'],
)

oozekit_dep = dependency('oozekit', method: 'pkg-config', required: true)

executable('hello-ooze',
  'main.c',
  dependencies: [oozekit_dep],
  install: false,
)
