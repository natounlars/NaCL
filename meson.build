project('mc-launcher', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11'])

gnome = import('gnome')

gtk4_dep      = dependency('gtk4')
adw_dep       = dependency('libadwaita-1', version : '>=1.4')
glib_dep      = dependency('glib-2.0')
gio_dep       = dependency('gio-2.0')
json_glib_dep = dependency('json-glib-1.0')
soup_dep      = dependency('libsoup-3.0')

resources = gnome.compile_resources(
  'mc-launcher-resources',
  'src/resources/resources.gresource.xml',
  source_dir : 'src/resources',
  c_name : 'mc_launcher')

executable('mc-launcher',
  sources : [
    'src/main.c',
    'src/mc-application.c',
    'src/mc-config.c',
    'src/mc-instance.c',
    'src/mc-mod.c',
    'src/mc-library.c',       # 新增
    'src/mc-version-meta.c',  # 新增
    'src/mc-installer.c',     # 新增
    'src/mc-download-window.c',  # 新增
    'src/mc-instance-manager.c',   # 新增
    'src/mc-instance-window.c',    # 新增
    'src/mc-auth.c',
    'src/mc-auth-window.c',   # 新增
    'src/mc-launcher.c',
    'src/mc-version.c',
    'src/mc-downloader.c',
    resources[0],
  ],
  dependencies : [gtk4_dep, adw_dep, glib_dep, gio_dep, json_glib_dep, soup_dep],
  install : true)
