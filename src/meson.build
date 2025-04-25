rpm_plugindir = rpm_dep.get_variable(pkgconfig: 'rpmplugindir')

configure_file(
    input: 'config.h.in',
    output: 'config.h',
    configuration : {
        'log_path' : log_path,
    }
)

shared_module(
  short_name,
  short_name + '.c',
  dependencies: [rpm_dep, glib_dep],
  name_prefix: '',
  install: true,
  install_dir: rpm_plugindir
)
