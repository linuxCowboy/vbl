project('VBindiff for Linux', 'cpp', license : 'GPL-2.0-or-later',
        version: run_command('version.sh', check: false).stdout())

if meson.version().version_compare('< 0.54.0')
        error('meson too old')
endif

curses_dep = [dependency('panelw'), dependency('ncursesw')]

cpp_extra = ['-m64']

executable('vbl', 'vbl.cpp', dependencies: curses_dep, cpp_args: cpp_extra)
