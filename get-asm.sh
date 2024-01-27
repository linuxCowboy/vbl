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

cat $FILE |
sed '/^\s*\.cfi_/d' |
sed '/^\s*\.loc /d' |
sed '/^\.L[BEFV]/d' |
c++filt             > "$MESON_BUILD_ROOT/${PROJECT}_asm.lst"

