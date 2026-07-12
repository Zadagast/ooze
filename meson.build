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

mutter_typelibdir = mutter_dep.get_variable('typelibdir')

executable('my-desktop',
  'src/main.c',
  'src/my-plugin.c',
  'src/my-aqua-draw.c',
  'src/my-dock.c',
  'src/my-theme.c',
  'src/my-window.c',
  c_args: c_args,
  dependencies: [
    mutter_dep,
    clutter_dep,
    cogl_dep,
    mtk_dep,
    gio_dep,
    gio_unix_dep,
    gdk_pixbuf_dep,
    libpng_dep,
    dependency('cairo'),
    dependency('pango'),
    dependency('pangocairo'),
  ],
  build_rpath: mutter_typelibdir,
  install: true,
)
