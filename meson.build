project('rpm-plugin-filechange', 'c', version: '1.0')
short_name = 'filechange'

rpm_dep = dependency('rpm', required: true)
glib_dep = dependency('glib-2.0', required: true)

log_path = get_option('log')

subdir('src')
subdir('macros')
