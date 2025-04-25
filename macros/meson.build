rpm_macrodir = rpm_dep.get_variable(pkgconfig: 'rpmhome') / 'macros.d'

configure_file(
    input: 'macros.in',
    output: 'macros.transaction_' + short_name,
    configuration : {
        'short_name' : short_name,
    },
    install_dir: rpm_macrodir
)
