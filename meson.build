project('htmdict', 'c',
  version: '0.2.0',
  meson_version: '>= 0.59.0',
  default_options: ['warning_level=2', 'c_std=gnu11', 'buildtype=debugoptimized'],
)

gtk_dep = dependency('gtk4')
adw_dep = dependency('libadwaita-1')
webkit_dep = dependency('webkitgtk-6.0', required: false)
if not webkit_dep.found()
  webkit_dep = dependency('webkit2gtk-4.1')
endif
zlib_dep = dependency('zlib')
lzo_dep = dependency('lzo2')
glib_dep = dependency('glib-2.0')
sqlite3_dep = dependency('sqlite3')
json_dep = dependency('json-glib-1.0')

srcs = [
  'src/dict-cache.c',
  'src/dict-cache-builder.c',
  'src/zip-mmap.c',
  'src/htm-dict.c',
  'src/main.c',
  'src/flat-index.c',
  'src/dict-fts-index.c',
]

executable('htmdict', srcs,
  include_directories: include_directories('src'),
  dependencies: [gtk_dep, adw_dep, webkit_dep, zlib_dep, sqlite3_dep, json_dep],
  install: true,
)

executable('mdx2diction', 
  ['src/mdx2diction.c', 'src/mdict-parser.c', 'src/ripemd128.c'],
  dependencies: [glib_dep, lzo_dep, zlib_dep],
  install: true)
