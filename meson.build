project('VBindiff for Linux', 'cpp', license : 'GPL-2.0-or-later',
        default_options : ['cpp_std=c++11', 'buildtype=debugoptimized'],
        version: run_command('version.sh', check: false).stdout())

# meson compile
if meson.version().version_compare('< 0.54.0')
        error('meson too old')
endif

    curses_dyn = [dependency('ncursesw')]

    curses_sta = [dependency('ncursesw', static: true)]

    asm_list   = ['-save-temps', '-fverbose-asm', '-masm=intel']

    obj        = \
executable('vbl',            'vbl.cpp',    dependencies: curses_dyn,
        cpp_args: ['-m64', asm_list])

    lnk        =             obj.extract_objects('vbl.cpp')

executable('vbl-strip',      objects: lnk, dependencies: curses_dyn,
        link_args: '-Wl,--strip-all')

executable('vbl-stat',       objects: lnk, dependencies: curses_sta,
        link_args: '-static')

executable('vbl-stat-strip', objects: lnk, dependencies: curses_sta,
        link_args: ['-static', '-Wl,--strip-all'])

custom_target('vbl-asm', input : obj, output : 'vbl_dis.lst',
        env : {'MESON_BUILD_ROOT': meson.current_build_dir()},
        command : ['get-asm.sh', '@INPUT@'],
        capture : true, build_by_default : true)

