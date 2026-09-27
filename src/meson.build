rpm_plugindir = rpm_dep.get_variable(pkgconfig: 'rpmplugindir')

shared_module(
  short_name,
  short_name + '.c',
  dependencies: [rpm_dep, glib_dep],
  name_prefix: '',
  install: true,
  install_dir: rpm_plugindir
)
