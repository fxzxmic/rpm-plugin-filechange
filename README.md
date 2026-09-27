# rpm-plugin-filechange

An RPM transaction plugin that logs files added or removed during package
upgrades and downgrades. Meson's `-Dlog` and `-Dexclude` options set the
defaults for the RPM macros `%_filechange_log` and `%_filechange_exclude`.
