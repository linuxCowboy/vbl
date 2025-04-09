vbl - VBindiff for Linux
========================

Hex viewer, differ, dumper and editor

dynamic 16/24/32 byte Hex & ASCII view in a terminal

256TB Files & Devices

edit / insert / delete

zero-tolerant _TurboSearch_ for SSD

SIMD speedup

diff mode

dump mode

64-bit static + dynamic

asm + disasm code

Features:
---------

 - Ascii search `f`
 - Binary search
 - Forward search `n`
 - Backward search `p`
 - Case insensitive `i`
 - Search history `Up` `Dn`
 - Search edit `Ins` `Del` `^u` `^k`
 - Search highlight
 - Search indentation
 - Search interruption `Esc`
 - Visual feedback
 - Goto position decimal `g`
 - Goto position percent
 - Goto position hex (abcd 0x1234 1234x)
 - Goto position 2** (kmgt)
 - Goto position 10** (KMGT)
 - Goto position *512 (s)
 - Goto position *4096 (S)
 - Goto position offset (+addr -addr)
 - Goto position history `Up` `Dn`
 - Goto last address `'` `<`
 - Goto last offset `.`
 - Goto last offset neg `,`
 - Set  last address `l`
 - Set  last address auto `g` `f` `Home` `End`
 - Set  jump address `j`
 - Get  jump address `"`
 - Next difference `Enter`
 - Prev difference `#` `\`
 - Next different byte `PgDn`
 - Prev different byte `PgUp`
 - Sync 1. with 2. view `1`
 - Sync 2. with 1. view `2`
 - File position decimal
 - File position percent
 - File offset difference
 - _Smartscroll_ (single mode) `ENTER`
 - Skip forward 4% `+` `*` `=`
 - Skip backward 1% `-`
 - ASCII-Mode (single mode) `a`
 - Column raster `r`
 - Edit file `e`
 - Edit insert byte `Ins`
 - Edit delete byte `Del`
 - RW/RO detection
 - Use only top file `t`
 - Use only bottom file `b`
 - Help window `h`
 - Quit `q`
 - Easter egg

Notes:
------

All operations take place in read-only mode.

Only if you _exit_ the edit mode and there are changes and you _explicitly_ confirm this will the file be temporarily opened for read/write.

With inserted or deleted bytes, the write can be huge, so it happens always **in place**.

The _last address_ is auto set with initial `Find`, `Goto` w/o relative, `home`/`end` or manual with `l`.

A fixed _jump address_ can be set with `j` and get with `"`; init with param + set last addr.

The starting point for _seek next diff byte_ is bottom-right.

The starting point for _seek prev diff byte_ is top-left; with added Page Up.

A _pane offset difference_ remains during comparison.

`Goto` position is now 2** kmgt and 10** KMGT(S.I.); with added sector support.

`Esc` can interrupt the searches.

Only `q` quit the program.

Build:
------

```
# headers + *meson* (debian)
apt install libncurses-dev meson

meson setup vbl

### change thousands separator from dot/default to comma:
# meson configure -Dcpp_args="-DTHOU_SEP_COMMA=1" vbl

meson compile -C vbl
```

Exe:
----

```
./vbl/vbl               64-bit executable
./vbl/vbl-strip
./vbl/vbl-stat          static version
./vbl/vbl-stat-strip
```

Asm:
----

```
vbl/vbl_asm.lst:        cleaned assembly

vbl/vbl_asm-diff.lst:   more cleaned for diff/dwdiff

vbl/vbl_dis.lst:        disassembly with color codes
```

Screenshoots:
-------------

![Screenshot](pics/one.jpg)  
*One File*

![Screenshot](pics/ascii.jpg)  
*ASCII Mode*

![Screenshot](pics/64gb.jpg)  
*Files >64GB*

![Screenshot](pics/two.jpg)  
*Two Files*

![Screenshot](pics/ask.png)  
*You have to confirm long writes (>512MB)*

![Screenshot](pics/help.png)  
*Help Screen*

Cmdline:
--------

```
VBinDiff for Linux 4.4

	vbl file [file2] [addr] [addr2]                     // ncurses

	vbl file1 file2 -                                   // diff view

	vbl file1 file2 --                                  // diff return

	vbl file -  [start [end]] [length{l$}] [width{w$}]  // dump ascii

	vbl file -- [start [end]] [length{l$}]              // dump binary

// type 'h' for help

```

Shortcut:
---------

```
vb () { [[ $2 ]] && vbl "$1" "$2" - || echo "vb file1 file2 [-]";}

```

