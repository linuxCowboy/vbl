project('VBindiff for Linux', 'cpp', license : 'GPL-2.0-or-later',
        version: run_command('version.sh', check: false).stdout())

# meson compile
if meson.version().version_compare('< 0.54.0')
        error('meson too old')
endif

curses_dyn = [dependency('ncursesw')]
curses_sta = [dependency('ncursesw', static: true)]

obj = executable('vbl', 'vbl.cpp', dependencies: curses_dyn, cpp_args: '-m64')
obj = obj.extract_objects('vbl.cpp')

executable('vbl-strip', objects: obj, dependencies: curses_dyn,
           link_args: '-Wl,--strip-all')

executable('vbl-stat', objects: obj, dependencies: curses_sta,
           link_args: '-static')

executable('vbl-stat-strip', objects: obj, dependencies: curses_sta,
           link_args: ['-static', '-Wl,--strip-all'])
