#!/bin/sh
#
# disassemble parameter to stdout
#
#       default: compiled dynamic exe
# +
#
# cleaning assembler source

[ $MESON_BUILD_ROOT ] && PROJECT=`basename $MESON_BUILD_ROOT`

FILE="$PROJECT/$PROJECT"

[ $1 ] && FILE=$1

[ -f $FILE ] &&

objdump --source-comment \
        --disassembler-options intel \
        --demangle \
        --line-numbers \
        --disassembler-color=on \
        --no-show-raw-insn \
        --visualize-jumps=color \
                $FILE

#####

FILE="$MESON_BUILD_ROOT/$PROJECT.p/$PROJECT.cpp.s"

[ -f $FILE ] &&

cat $FILE                         |
c++filt                           |
sed '/^\s*\.cfi_/d'               |
sed '/^\s*\.loc /d'               |
sed '/^\.L[BEFV]/d'               |
sed '/\.LVU/d'                    |
sed 's/_[0-9]\+/_d+/g'            |
sed 's/tmp[0-9]\+/tmpd+/g'        |
sed '/GNU/    {p;d};
     /printf/ {p;d};
     /#/       s/\.[0-9]\+/.d+/g' > "$MESON_BUILD_ROOT/${PROJECT}_asm.lst"
