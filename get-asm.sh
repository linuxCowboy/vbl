#!/bin/sh
#
# disassemble parameter to stdout
#
#       default: compiled dynamic exe
# +
#
# cleaning assembler source
#
#       optional tag

if [ "$MESON_BUILD_ROOT" ]; then
        FILE="$MESON_BUILD_ROOT/$1"

elif [ "${1#${1%.s}}" = ".s" ]; then
        FILS="$1"

elif [ "${1#${1%.S}}" = ".S" ]; then
        FILS="$1"

elif [ "$1" ]; then
        FILE="$1"

else
        exit
fi

[ -f "$FILE" ] &&

objdump --source-comment \
        --disassembler-options intel \
        --demangle \
        --line-numbers \
        --disassembler-color=on \
        --visualize-jumps=color \
                "$FILE"

#################################

ASM_DIR="/tmp/VBL"

[ "$2" ] && TAG="-$2"

if [ "$MESON_BUILD_ROOT" ]; then
        FILA="$MESON_BUILD_ROOT/$1.p/$1.cpp.s"
        LIST="$MESON_BUILD_ROOT/${1}_asm.lst"
        DIFF="$MESON_BUILD_ROOT/${1}_asm-diff.lst"

elif [ -f "$FILS" ]; then
        [ -d "$ASM_DIR" ] || mkdir -pv "$ASM_DIR" || exit

        FILA="$FILS"
        FILS=`basename "${FILS%.s}" .S`
        LIST="$ASM_DIR/$FILS-asm$TAG.lst"
        DIFF="$ASM_DIR/$FILS-asm$TAG-diff.lst"
fi

[ -f "$FILA" ] &&

cat "$FILA"                             |
c++filt                                 |

sed '/section\s*.debug_info/,$d'        |

sed '/^\s*\.cfi_/d'                     |
sed '/^\s*\.loc /d'                     |
sed '/^\.L[BEFV]/d'                     |
sed '/\.LVU/d'                          |
sed '/\.LVL/d'                          |
sed 's/_[0-9]\+/_d+/g'                  |
sed 's/tmp[0-9]\+/tmpD+/g'              |
sed '/#.*\.[0-9]/ s/\.[0-9]\+/.D+/g'    |

        cat > "$LIST" || exit 0  # test/debug expressions
#        exit

# kick line numbers/labels
[ -f "$LIST" ] &&

perl -pe '
        s%( \.\./[\w.-]*?cpp):\d+:%$1:d+:%;

        s/\.L\d+/.Ld+/g;

' "$LIST" > "$DIFF"

