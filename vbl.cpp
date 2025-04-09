//--------------------------------------------------------------------
//
//   VBinDiff for Linux
//
//   Hex viewer, differ, dumper and editor
//
//   Copyright 2021-2025 by linuxCowboy
//
//   vbindiff       by Christopher J. Madsen
//   64GB           by Bradley Grainger
//   dynamic width  by Christophe Bucher
//
//   Version:
//      1.x     classic vbindiff interface, fix 32 byte
//      -----------------------------------------------
//      2.0     dynamic 16/24/32 byte width
//      2.1     256 terabyte files
//      2.2     kick panels
//      2.3     ascii mode
//      2.4     speedup differ
//      2.5     full help
//      2.6     cursor color
//      2.7     use deque
//      2.8     kick map
//      2.9     kick iostream
//      2.10    kick sstream
//      2.11    kick algorithm
//      2.12    ignore case
//      2.13    relative jumps
//      2.14    goto back
//      2.15    repeat offset
//      2.16    seekNotChar ascii
//      ---- meanwhile almost completely rewritten ----
//      3.0     edit insert/delete
//      3.1     InputManager
//      3.2     progress bar
//      3.3     goto prefix
//      3.4     edit diff
//      3.5     set last
//      3.6     golf search
//      3.6.1   turbo zero
//      3.6.2   SIMD case
//      3.7     start addr
//      ------------------
//      4.0     SSE2 SIMD
//      4.1     search all
//      4.2     jump addr
//      4.3     dump mode
//      4.4     diff mode
//
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the GNU General Public License as
//   published by the Free Software Foundation; either version 2 of
//   the License, or (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   For the GNU General Public License see <https://www.gnu.org/licenses/>.
//--------------------------------------------------------------------------

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>
#include <x86gprintrin.h>
#include <emmintrin.h>
#include <ncurses.h>

#include <string>
#include <deque>

using namespace std;

#define VBL_VERSION     "4.4"

// ###################
// ##### options #####
// ###################

/* set Cursor Color in input window with Operating System Command */
#ifndef SET_CURSOR_COLOR
#define SET_CURSOR_COLOR        0
#endif

/* show a summary in edit insert/delete after large writes + wait */
#ifndef SHOW_WRITE_SUMMARY
#define SHOW_WRITE_SUMMARY      0
#endif

/* thousands separator (or '\0') */
#define THOU_SEP                '.'

// ###################

/* const: gcc optimizes macros away */
const bool debug = 0;  //  ##:q

/* curses debug:
f=/tmp/.vbl
tail -F $f
vbl file 2>$f; cat $f
*/
#define mPI(x)          if (debug)  fprintf(stderr, "\r%s: 0x%lX %ld\n",  #x, (long)  x, (long)  x);
#define mPU(x)          if (debug)  fprintf(stderr, "\r%s: 0x%lX %lu\n",  #x, (Full)  x, (Full)  x);
#define mPF(x)          if (debug)  fprintf(stderr, "\r%s: %f\n",         #x, (float) x);
#define mPS(x)          if (debug)  fprintf(stderr, "\r%s: %s\n",         #x,         x);

#define mPsec(x)        if (debug)  fprintf(stderr, "\r%s: %ld sec\n",    #x, (long)  x);
#define mPms(x)         if (debug)  fprintf(stderr, "\r%s: %.3f msec\n",  #x, (float) x / 1000000);

/* profiling: sec, msec; init: {1,} */
#define mTs(x,init...)  if (debug) {if (1 != 1##init) {x = time(NULL);}\
                                    else              {x = time(NULL) - x; mPsec(x)}}
#define mTms(x,init...) if (debug) {if (1 != 1##init) {x = timer();}\
                                    else              {x = timer(2,x); mPms(x)}}  // one-shot

#define mZ(x)           if (debug)                    {x = 0;}                          // reset
#define mTpp(x,init...) if (debug) {if (1 != 1##init) timer(1); else {x += timer(2);}}  // sum-up (t0)

/* w/o redirection */
#define mPP(x)          if (debug)  sleep(x);
#define mPK             if (debug)  file1.readKeyF();

/* hex dump: pointer, count */
#define mPX(x, c)       if (debug) {fprintf(stderr, "\r%s, %d \n\r", #x, (int) c);\
                                    for (Word I=0; I < c; ++I) fprintf(stderr, "%X ", (Byte) (x)[I]);\
                                    fprintf(stderr, "\n");}

#define mCeil(x, y)     x / y + (x % y ? 1 : 0)

#define mEdit           historyPos = history.size();
#define mScale          count / (scale ? scale : 1)

#define KEY_CTRL_C      0x03
#define KEY_TAB         0x09
#define KEY_CTRL_K      0x0B
#define KEY_RETURN      0x0D
#define KEY_CTRL_U      0x15
#define KEY_ESCAPE      0x1B
#define KEY_DELETE      0x7F

//====================================================================
// Color Enumerations

enum ColorPair {
        pairWhiteBlue = 1,
        pairBlackWhite,
        pairRedWhite,
        pairYellowBlue,
        pairGreenBlue,
        pairBlackCyan,
        pairGreenBlack,
        pairWhiteCyan,
        pairWhiteRed,
        pairWhiteGreen,
        pairBlackYellow
};

enum Style {
        cMainWin,
        cInputWin,
        cHelpWin,
        cName,
        cDiff,
        cEdit,
        cInsert,
        cSearch,
        cSeek,
        cMatch,
        cRaster,
        cAddress,
        cHotkey,
        cHighFile,
        cHighBusy,
        cHighBus2,
        cHighEdit
};

static const ColorPair colorStyle[] = {
        pairWhiteBlue,   // cMainWin
        pairWhiteBlue,   // cInputWin
        pairWhiteBlue,   // cHelpWin
        pairBlackWhite,  // cName
        pairGreenBlack,  // cDiff
        pairYellowBlue,  // cEdit
        pairGreenBlue,   // cInsert
        pairWhiteRed,    // cSearch
        pairWhiteGreen,  // cSeek
        pairRedWhite,    // cMatch
        pairBlackCyan,   // cRaster
        pairYellowBlue,  // cAddress
        pairGreenBlue,   // cHotkey
        pairWhiteCyan,   // cHighFile
        pairWhiteRed,    // cHighBusy
        pairWhiteGreen,  // cHighBus2
        pairBlackYellow  // cHighEdit
};

static const attr_t attribStyle[] = {
                    COLOR_PAIR(colorStyle[ cMainWin  ]),
                    COLOR_PAIR(colorStyle[ cInputWin ]),
                    COLOR_PAIR(colorStyle[ cHelpWin  ]),
                    COLOR_PAIR(colorStyle[ cName     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cDiff     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cEdit     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cInsert   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cSearch   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cSeek     ]),
                    COLOR_PAIR(colorStyle[ cMatch    ]),
                    COLOR_PAIR(colorStyle[ cRaster   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cAddress  ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHotkey   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHighFile ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHighBusy ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHighBus2 ]),
                    COLOR_PAIR(colorStyle[ cHighEdit ])
};

//====================================================================
// Type definitions

typedef unsigned char   Byte;
typedef unsigned short  Word;
typedef unsigned int    Half;
typedef unsigned long   Full;
typedef __m128i         Quad;
typedef Byte            Command;

typedef int             File;
typedef off_t           FPos;  // long int
typedef ssize_t         Size;  // long int

typedef deque<string>   StrDeq;
typedef deque<Byte>     BytDeq;

enum LockState { lockNeither, lockTop, lockBottom };

//====================================================================
// Constants  ##:cmd

const Command   cmgGoto        = 0x80;  // Main cmd
const Command   cmgGotoTop     = 0x08;  // Flag
const Command   cmgGotoBottom  = 0x04;  // Flag
const Command   cmgGotoForw    = 0x40;
const Command   cmgGotoBack    = 0x20;
const Command   cmgGotoMask    = 0x13;  // Mask
const Command   cmgGotoLGet    = 0x01;
const Command   cmgGotoLSet    = 0x02;
const Command   cmgGotoLOff    = 0x03;
const Command   cmgGotoNOff    = 0x11;
const Command   cmgGotoJGet    = 0x12;
const Command   cmgGotoJSet    = 0x13;

const Command   cmfFind        = 0x40;  // Main cmd
const Command   cmfFindNext    = 0x20;
const Command   cmfFindPrev    = 0x10;
const Command   cmfNotCharDn   = 0x02;
const Command   cmfNotCharUp   = 0x01;

const Command   cmmMove        = 0x20;  // Main cmd
const Command   cmmMoveForward = 0x10;
const Command   cmmMoveMask    = 0x03;  // Mask
const Command   cmmMoveByte    = 0x00;  // Move 1 byte
const Command   cmmMoveLine    = 0x01;  // Move 1 line
const Command   cmmMovePage    = 0x02;  // Move 1 page
const Command   cmmMoveAll     = 0x03;  // Move to begin or end

const Command   cmNothing      =  0;
const Command   cmUseTop       =  1;
const Command   cmUseBottom    =  2;
const Command   cmNextDiff     =  3;
const Command   cmPrevDiff     =  4;
const Command   cmEditTop      =  5;
const Command   cmEditBottom   =  6;
const Command   cmSyncUp       =  7;
const Command   cmSyncDn       =  8;
const Command   cmShowAscii    =  9;
const Command   cmIgnoreCase   = 10;
const Command   cmShowRaster   = 11;
const Command   cmShowHelp     = 12;
const Command   cmSmartScroll  = 13;
const Command   cmQuit         = 14;

//--------------------------------------------------------------------

const Size minScreenHeight = 24,  // Enforced minimum height
           minScreenWidth  = 79,  // Enforced minimum width

           skipForw = 4,  // Percent to skip forward
           skipBack = 1,  // Percent to skip backward

           staticSize = 1L << 25,  // size global buffers
           warnResize = 1L << 29,  // confirmation threshold

           barDelay = 6,  // msec, smoothness vs. speed

           maxHistory = 20,  // find and goto

           dumpDef = 16,  // terminal dump: both
           dumpMax = 32;  // full dump only

const char *hexDigits     = "0123456789ABCDEF",                       // search
           *hexDigitsGoto = "0123456789ABCDEFabcdef%Xx+-kmgtKMGTsS",  // goto

           *colorInsert = "#00BBBB",  // cursor color "normal"
           *colorDelete = "#EE0000";  // cursor color "very visible"

const wchar_t barSyms[] = {L'▏', L'▎', L'▍', L'▌', L'▋', L'▊', L'▉', L'█'};

const char sPrefix[] = "skmgtSKMGT";
const Size aPrefix[] = {  512, 1024, 1048576, 1073741824, 1099511627776,
                         4096, 1000, 1000000, 1000000000, 1000000000000 };

//--------------------------------------------------------------------
// Help screen text - max 21 lines (minScreenHeight - 3)  ##:x

const char *aHelp[] = {
"  ",
"  Move:  left right up down   home end    space backspace",
"  ",
"  Find   Next Prev       PgDn PgUp == next/prev diff byte",
"  ",
"  Goto [+-]{dec hex 0x x$}[%|sSkmgtKMGT] +4% + * =  -1% -",
"  Last addr: ' <   Jump Addr: \"   last off: .  neg off: ,",
"  ",
"  Edit file   show Raster   Ignore case              Quit",
"  ",
"                      --- One File ---",
"  Enter == sm4rtscroll   Ascii mode",
"  ",
"                      --- Two Files ---",
"  Enter == next diff  # \\ == prev diff  1 2 == sync views",
"                      use only Top,  use only Bottom",
"  ",
"                      --- Edit ---",
"  Enter == copy byte from other file;     Insert   Ctrl-U",
"  Tab  ==  HEX <> ASCII, Esc == done;     Delete   Ctrl-K",
"  "
};

const int longestLine = 57;  // adjust!

const Byte aBold[] = {  // hotkeys, start y:1, x:1
        4,3,  4,10, 4,15,
        6,3,  6,46, 6,48, 6,50,  6,57,
        7,3, 7,14, 7,16,  7,20, 7,31,  7,45, 7,57,
        9,3,  9,20,  9,29,  9,54,
        12,26,
        15,23, 15,25,  15,41, 15,43,
        16,32, 16,47,
        0
};

const char *helpVersion = " VBinDiff for Linux " VBL_VERSION " ";

const int helpWidth  = 1 + longestLine + 2                  + 1,
          helpHeight = 1 + sizeof(aHelp) / sizeof(aHelp[0]) + 1;

//====================================================================
// Global Variables  ##:vars

WINDOW *winInput,
       *winHelp;

alignas(0x1000)
Byte bufFile1[staticSize],
     bufFile2[staticSize];

Byte *buffer = bufFile1;

FPos *sm4rt;

char bufTimer[64];

bool singleFile,
     showRaster,
     sizeTera,
     modeAscii,
     ignoreCase,
     stopRead,
     useSSE2,
     haveDiff;

LockState lockState;

string lastSearch,
       lastSearchIgnCase;

StrDeq hexSearchHistory,
       textSearchHistory,
       positionHistory;

BytDeq editBytes,
       editColor;

// Set dynamically for 16/24/32 byte width
Size screenWidth,   // Number of columns in curses
     linesTotal,    // Number of lines in curses
     numLines,      // Number of lines of each file to display
     bufSize,       // Number of bytes of each file to display
     lineWidth,     // Number of bytes displayed per line
     lineWidthAsc,  // Number of bytes displayed per line ascii
     inWidth,       // Number of digits in input window
     leftMar,       // Starting column of hex display
     leftMar2,      // Starting column of ASCII display
     searchIndent,  // Lines of search result indentation
     steps[4],      // Number of bytes to move for each step

     diffMode,      // diff files to terminal
     dumpMode,      // dump file to terminal
     dumpBeg,
     dumpEnd,
     dumpLen,
     dumpWid;

// debug timer 1-9, init 0
__attribute__ ((unused)) static Size t1, t2, t3, t4, t5, t6, t7, t8, t9, t0;

char *program,

#if THOU_SEP_COMMA
     thouSep = ',';
#else
     thouSep = THOU_SEP;
#endif

//====================================================================
// Global Functions

//--------------------------------------------------------------------
// Global timer / profiling
// modes: 0-set 1-sum_up 2-nsec 3-msec

Size timer(int mode=0, Size var=t0)
{
        timespec ts;

        clock_gettime(CLOCK_TAI, &ts);

        Size ret = ts.tv_sec * 1000000000 + ts.tv_nsec;

        if (mode == 1) {
                t0 = ret;
        }

        else if (mode == 2) {
                ret -= var;
        }

        else if (mode == 3) {
                ret = (ret - var) / 1000000;
        }

        return ret;
}

//--------------------------------------------------------------------
// FileIO

File OpenFile(const char* path, bool writable=false)
{
        return open(path, (writable ? O_RDWR : O_RDONLY));
}

bool WriteFile(File file, const Byte* buf, Size cnt)
{
        while (cnt > 0) {
                Size bytesWritten = write(file, buf, cnt);

                if (bytesWritten < 1) {
                        if (errno == EINTR)
                                bytesWritten = 0;
                        else
                                return false;
                }

                buf += bytesWritten;
                cnt -= bytesWritten;
        }

        return true;
}

Size ReadFile(File file, Byte* buf, Size cnt)
{
        Size ret = read(file, buf, cnt);

        /* interrupt the searches */
        timeout(0);
        switch(getch()) {
                case KEY_ESCAPE:
                        stopRead = true;
        }
        timeout(-1);

        /* mitigate read errors */
        if (ret < 0) {
                ret      = 0;
                stopRead = true;
        }

        return ret;
}

FPos SeekFile(File file, FPos position, int whence=SEEK_SET)
{
        return lseek(file, position, whence);
}

//--------------------------------------------------------------------
// Dumper (no curses)

void dumpFile(char* file)
{
        File fd;

        if ((fd = OpenFile(file)) < 0) {
                err(2, file);
        }

        if (! dumpLen) {
                if (! dumpEnd) {
                        dumpEnd = SeekFile(fd, 0, SEEK_END);
                }

                dumpLen = dumpEnd - dumpBeg;
        }

        if (SeekFile(fd, dumpBeg) < 0) {
                err(3, "seek");
        }

        Size bytesRead, cnt, len;

        if (dumpMode == 2) {  // binary
                Byte* pb = buffer;

                for (; ; dumpLen -= cnt) {
                        if ((bytesRead = read(fd, pb, staticSize)) < 0) {
                                err(4, "read");
                        }

                        if (! (cnt = min(bytesRead, dumpLen))) {
                                break;
                        }

                        for (Size i=0; i < cnt; ++i) {
                                putchar(pb[i]);
                        }
                }
        }

        else {
                Size tera = dumpBeg + dumpLen >= 68719476736 ? 3 : 0;

                if (dumpWid > dumpMax) {
                       dumpWid = dumpMax;
                }

                char addr[9];
                sprintf(addr, "%%0%dlX ", tera ? 12 : 9);

                Size lcnt = 9 + tera + 2 + (dumpWid - 1) / 8 + dumpWid * 3 + 1 + dumpWid;
                char line[lcnt + 1] = { 0 };

                for (; ; dumpLen -= cnt, dumpBeg += cnt) {
                        if ((bytesRead = read(fd, buffer, staticSize)) < 0) {
                                err(4, "read");
                        }

                        if ((cnt = min(bytesRead, dumpLen)) <= 0) {
                                printf(addr, dumpBeg);

                                putchar(10);
                                break;
                        }

                        for (Size i=0; i < cnt; i += len) {
                                if (i > cnt - dumpWid) {
                                        memset(line, ' ', lcnt);
                                }

                                char *pbufHex = line;

                                pbufHex += sprintf(line, addr, dumpBeg + i);

                                len = min(cnt - i, dumpWid);

                                for (Size j=0; j < len; ++j) {
                                        if (! (j % 8)) {
                                                *pbufHex++ = ' ';
                                        }

                                        Byte b = buffer[i + j];

                                        pbufHex += sprintf(pbufHex, "%02X ", b);

                                        line[lcnt - dumpWid + j] = b > '~' || b < ' ' ? '.' : b;
                                }

                                *pbufHex = ' ';

                                puts(line);
                        }
                }
        }

        close(fd);

        exit(0);
} // end dumpFile

//--------------------------------------------------------------------
// Cmdline differ

FPos diffFile(char* file1, char* file2)
{
        File fd1, fd2;

        if ((fd1 = OpenFile(file1)) < 0) {
                err(2, file1);
        }

        if ((fd2 = OpenFile(file2)) < 0) {
                close(fd1);
                err(2, file2);
        }

        FPos off = 0;
        Size size, size1, size2;

        do {
                if ((size1 = read(fd1, bufFile1, staticSize)) < 0) {
                        err(4, file1);
                }

                if ((size2 = read(fd2, bufFile2, staticSize)) < 0) {
                        err(4, file2);
                }

                if (size1 == 0 && size2 == 0) {
                        off = -1;
                        break;
                }

                size = min(size1, size2);

                if (size == 0) {
                        break;
                }

                if (memcmp(bufFile1, bufFile2, size)) {
                        break;
                }

                if (size < staticSize) {
                        if (size1 == size2) {
                                off = -1;
                        }
                        break;
                }

                off += staticSize;

        } while (1);

        close(fd1);
        close(fd2);

        return off;
} // end diffFile

//--------------------------------------------------------------------
// Initialize ncurses  ##:i

bool initialize()
{
        setlocale(LC_ALL, "");  // for Unicode blocks

        if (! initscr()) {
                return false;
        }

        set_escdelay(10);
        keypad(stdscr, true);

        nonl();
        cbreak();
        noecho();

        if (has_colors()) {
                start_color();

                init_pair(pairWhiteBlue,   COLOR_WHITE,  COLOR_BLUE);
                init_pair(pairBlackWhite,  COLOR_BLACK,  COLOR_WHITE);
                init_pair(pairRedWhite,    COLOR_RED,    COLOR_WHITE);
                init_pair(pairYellowBlue,  COLOR_YELLOW, COLOR_BLUE);
                init_pair(pairGreenBlue,   COLOR_GREEN,  COLOR_BLUE);
                init_pair(pairBlackCyan,   COLOR_BLACK,  COLOR_CYAN);
                init_pair(pairGreenBlack,  COLOR_GREEN,  COLOR_BLACK);
                init_pair(pairWhiteCyan,   COLOR_WHITE,  COLOR_CYAN);
                init_pair(pairWhiteRed,    COLOR_WHITE,  COLOR_RED);
                init_pair(pairWhiteGreen,  COLOR_WHITE,  COLOR_GREEN);
                init_pair(pairBlackYellow, COLOR_BLACK,  COLOR_YELLOW);
        }

        curs_set(0);

        return true;
} // end initialize

//--------------------------------------------------------------------
// Visible difference between insert and overstrike mode

void showCursor(bool over=false)
{
        over ? curs_set(2) : curs_set(1);

#if SET_CURSOR_COLOR
        over ? printf("\e]12;%s\a", colorDelete) : printf("\e]12;%s\a", colorInsert);

        fflush(stdout);
#endif
}

void hideCursor()
{
        curs_set(0);
}

//--------------------------------------------------------------------
// Shutdown ncurses

void shutdown()
{
        free(sm4rt);

        delwin(winInput);
        delwin(winHelp);

        showCursor();

        endwin();
}

//--------------------------------------------------------------------
// Error exit ncurses

void exitMsg(int status, const char* message)
{
        shutdown();

        errx(status, message);
}

//--------------------------------------------------------------------
// Reset variables for ascii mode

void setViewMode()
{
        lineWidth = modeAscii ? lineWidthAsc : lineWidthAsc / 4;

        bufSize = numLines * lineWidth;

        searchIndent = lineWidth * 3;

        steps[cmmMoveByte] = 1;
        steps[cmmMoveLine] = lineWidth;
        steps[cmmMovePage] = bufSize - lineWidth;
        steps[cmmMoveAll]  = 0;
}

//--------------------------------------------------------------------
// Set variables for dynamic width  ##:y

void calcScreenLayout()
{
        if (COLS < minScreenWidth) {
                string err("The screen must be at least " + to_string(minScreenWidth) + " characters wide.");

                exitMsg(31, err.c_str());
        }

        if (LINES < minScreenHeight) {
                string err("The screen must be at least " + to_string(minScreenHeight) + " lines high.");

                exitMsg(32, err.c_str());
        }

        short tera = sizeTera ? 3 : 0;  // use large addresses only if needed

        leftMar = 11 + tera;

        if (COLS >= 140 + tera) {
                lineWidth   = 32;
                screenWidth = 140 + tera;
                leftMar2    = 108 + tera;
        }
        else if (COLS >= 108 + tera) {
                lineWidth   = 24;
                screenWidth = 108 + tera;
                leftMar2    = 84  + tera;
        }
        else {
                lineWidth   = 16;
                screenWidth = 76 + tera;
                leftMar2    = 60 + tera;
        }

        lineWidthAsc = lineWidth * 4;

        inWidth = (sizeTera ? 15 : 11) + 1;  // sign

        linesTotal = LINES;

        numLines = linesTotal / (singleFile ? 1 : 2) - 1;

        setViewMode();
} // end calcScreenLayout

//--------------------------------------------------------------------
// Convert a character to uppercase

int upCase(int c)
{
        return (c >= 'a' && c <= 'z') ? c & ~0x20 : c;
}

//--------------------------------------------------------------------
// Convert buffer to lowercase

void lowCase(Byte* buf, Size len)
{
        Byte* b = bufFile1;  // movdqu ==> movdqa

        if (len == staticSize) {  // SIMD
                for (Size i=0; i < staticSize; ++i) {
                        b[i] = b[i] >= 'A' && b[i] <= 'Z' ? b[i] | 0x20 : b[i];
                }
        }

        else {
                for (Size i=0; i < len; ++i) {
                        if (buf[i] <= 'Z' && buf[i] >= 'A') {
                                buf[i] |= 0x20;
                        }
                }
        }
}

//--------------------------------------------------------------------
// Convert buffer for ascii

void setAscii(Size len)
{
        Byte* b = bufFile1;  // enregister

        if (len == staticSize) {  // SIMD
                for (Size i=0; i < staticSize; ++i) {
                        b[i] = b[i] > '~' || b[i] < ' ' ? ' ' : b[i];
                }
        }

        else {
                for (Size i=0; i < len; ++i) {
                        if (b[i] > '~' || b[i] < ' ') {
                                b[i] = ' ';
                        }
                }
        }
}

//--------------------------------------------------------------------
// Compare lines for smartScroll:  16 / 32 / 64 / 96 / 128 Byte

bool cmpLine(Byte* ln1, Byte* ln2, Full cnt)
{
        Quad q1 = _mm_load_si128((Quad*) ln1),
             q2 = _mm_load_si128((Quad*) ln2);

        q1 = _mm_cmpeq_epi8(q1, q2);

        if (_mm_movemask_epi8(q1) != 0xFFFF) {
                return true;
        }

        if (cnt == 16) {
                return false;
        }

        q1 = _mm_load_si128((Quad*) (ln1 + 16));
        q2 = _mm_load_si128((Quad*) (ln2 + 16));

        q1 = _mm_cmpeq_epi8(q1, q2);

        if (_mm_movemask_epi8(q1) != 0xFFFF) {
                return true;
        }

        for (Full i = 32; i < cnt; i += 16) {
                q1 = _mm_load_si128((Quad*) (ln1 + i));
                q2 = _mm_load_si128((Quad*) (ln2 + i));

                q1 = _mm_cmpeq_epi8(q1, q2);

                if (_mm_movemask_epi8(q1) != 0xFFFF) {
                        return true;
                }
        }

        return false;
} // end cmpLine

//--------------------------------------------------------------------
// Compare lines for smartScroll (unaligned):  24 Byte

bool cmpLineU(Byte* ln1, Byte* ln2, Full cnt)
{
        Quad q1 = _mm_loadu_si128((Quad*) ln1),
             q2 = _mm_loadu_si128((Quad*) ln2);

        q1 = _mm_cmpeq_epi8(q1, q2);

        if (_mm_movemask_epi8(q1) != 0xFFFF) {
                return true;
        }

        q1 = _mm_loadu_si128((Quad*) (ln1 + 8));
        q2 = _mm_loadu_si128((Quad*) (ln2 + 8));

        q1 = _mm_cmpeq_epi8(q1, q2);

        if (_mm_movemask_epi8(q1) != 0xFFFF) {
                return true;
        }

        return false;
}

//--------------------------------------------------------------------
// Convert hex string to bytes

int packHex(char* buf)
{
        Byte *pb = (Byte*) buf,
             *po = pb;

        for (Byte b; (b = *pb); ++pb) {
                if (b == ' ') {
                        continue;
                }
                else {
                        b = (b - (*pb++ > 64 ? 55 : 48)) << 4;

                        b |= *pb - (*pb > 0x40 ? 0x37 : 0x30);

                        *po++ = b;
                }
        }

        return po - (Byte*) buf;
}

//--------------------------------------------------------------------
// My pretty printer

char *pretty(char *buffer, FPos *size, int sign)
{
        char aBuf[64],
             *pa = aBuf,
             *pb = buffer;

        sprintf(aBuf, (sign ? "%+ld" : "%ld"), *size);

        int len = strlen(aBuf);

        while (len) {
                *pb++ = *pa++;

                if (sign) {
                        --sign;
                        --len;
                }
                else {
                        if (--len && ! (len % 3)) {
                                if (thouSep) {
                                        *pb++ = thouSep;
                                }
                        }
                }
        }
        *pb = 0;

        return buffer;
} // end pretty

//--------------------------------------------------------------------
// Help window

void displayHelp()
{
        touchwin(winHelp);
        wrefresh(winHelp);
        wgetch(winHelp);
}

//--------------------------------------------------------------------
// Position the input window

void positionInWin(Command cmd, short width, const char *title, short height=3)
{
        if (wresize(winInput, height, width) != OK) {
                exitMsg(41, "Failed to resize window.");
        }

        wbkgd(winInput, attribStyle[cInputWin]);
        werase(winInput);

        mvwin(winInput,
                ((! singleFile && (cmd & cmgGotoBottom))
                      ? ((cmd & cmgGotoTop)
                              ? numLines                  // Moving both
                              : numLines + numLines / 2)  // Moving bottom
                      : (numLines - 1 ) / 2),             // Moving top
                 (screenWidth - width) / 2);

        box(winInput, 0, 0);

        mvwaddstr(winInput, 0, (width - strlen(title)) / 2, title);
}

//====================================================================
// Class ConWindow  ##:win

class ConWindow
{
    protected:
        WINDOW         *winW;

    public:
        ConWindow()                                                     {}
       ~ConWindow()                                                     { delwin(winW); winW = NULL; }

        void            initW(short x, short y, short width, short height, Style style);
        void            updateW()                                       { touchwin(winW); wrefresh(winW); }
        int             readKeyW()                                      { return wgetch(winW); }

        void            put(short x, short y, const char* s)            { mvwaddstr(winW, y, x, s); }
        void            putAttribs(short x, short y, Style color, short count) {
                                mvwchgat(winW, y, x, count, attribStyle[color], colorStyle[color], NULL); }

        void            setCursor(short x, short y)                     { wmove(winW, y, x); }

}; // end ConWindow

//====================================================================
// Class ConWindow member functions

//--------------------------------------------------------------------
// Initialize the window

void ConWindow::initW(short x, short y, short width, short height, Style attrib)
{
        if (! (winW = newwin(height, width, y, x))) {
                exitMsg(21, "Failed to create main window.");
        }

        wbkgd(winW, attribStyle[attrib]);

        keypad(winW, TRUE);
}

//====================================================================
// Class FileDisplay  ##:file

class Difference;

class FileDisplay
{
    friend class Difference;

        ConWindow               cwinF;

        const Difference       *diffsF;

        File                    fd;
        bool                    editable;

        Byte                   *dataF;
        Byte                   *sAllF;
        int                     dataSize;
        FPos                    offset;
        FPos                    prevOffset;
        FPos                    diffOffset;
        FPos                    lastOffset;

        int                     seekup;
        int                     se4rch;
        bool                    se4rchAll;

    public:
        FPos                    searchOff;
        FPos                    scrollOff;
        FPos                    repeatOff;
        FPos                    startAddr;
        Size                    filesize;
        char                   *filename;
        bool                    two;

    public:
                FileDisplay()                           {}
               ~FileDisplay()                           { if (fd) close(fd); delete [] dataF; delete [] sAllF; }

        bool    setFile(char* FileName);
        void    initF(int y, const Difference* Diff);
        void    resizeF();
        void    updateF()                               { cwinF.updateW(); }
        int     readKeyF()                              { return cwinF.readKeyW(); }

        void    display();
        void    attr(short x, short y, Style color, short count) { cwinF.putAttribs(x, y, color, count); }
        void    busy(bool on, bool ic, bool np);
        void    highEdit(short count)                   { attr(0, 0, cHighEdit, count); }

        void    edit(const FileDisplay* other);
        void    editOut(short outOffset);
        bool    WriteTail(FPos start);
        bool    assure();
        void    progress1();
        void    progress(wchar_t* bar, int count, int delay, int stint);

        void    setLast()                               { lastOffset = offset; }
        void    setJump()                               { startAddr  = offset; }
        void    getLast()                               { FPos tmp   = offset; moveTo(lastOffset); lastOffset = tmp; }
        void    skip(bool upwards);
        void    sync(const FileDisplay* other);
        void    mark(Byte* searchFor, Size searchLen);

        void    move(FPos step)                         { moveTo(offset + step); }
        void    moveTo(FPos newOffset);
        void    moveToEnd()                             { moveTo(filesize - steps[cmmMovePage]); }

        void    moveForw(Byte* searchFor, Size searchLen);  // __attribute__ ((__target__ ("sse2")))
        void    moveBack(Byte* searchFor, Size searchLen);

        void    seekForw();
        void    seekBack();

        void    smartScroll();
}; // end FileDisplay

//====================================================================
// Class Difference

class Difference
{
    friend void FileDisplay::display();

    protected:
        Byte           *dataD;
        FileDisplay    *file1D;
        FileDisplay    *file2D;

    public:
                Difference(FileDisplay* File1, FileDisplay* File2):
                                file1D(File1), file2D(File2)            {}
               ~Difference()                                            { delete [] dataD; }
        void    resizeD()                                               { dataD = new Byte[bufSize]; }
        void    compute();
        void    differ(int cmd);
}; // end Difference

//====================================================================
// Object instantiation

FileDisplay     file1, file2;

Difference      diffs(&file1, &file2);

//====================================================================
// Class Difference member functions
//
// Compute differences  ##:u

void Difference::compute()
{
        memset(dataD, 0, bufSize);

        int size1 = file1D->dataSize,
            size2 = file2D->dataSize;

        Byte *buf1 = file1D->dataF,
             *buf2 = file2D->dataF,
             *data = dataD;  // enregister

        int size = min(size1, size2);

        int diff = 0;
        for (; diff < size; ++diff) {
                if (*buf1++ != *buf2++) {
                        data[diff] = true;
                }
        }

        size = max(size1, size2);

        if (diff < size) {
                memset(data + diff, true, size - diff);
        }

        haveDiff = true;
} // end Difference::compute

//--------------------------------------------------------------------
// Search differences Next/Prev

void Difference::differ(int cmd)
{
        File fd1 = file1D->fd,
             fd2 = file2D->fd;

        FPos of1 = file1D->offset,
             of2 = file2D->offset;

        if (cmd == cmNextDiff) {
                if (haveDiff) {  // advance
                        Size diff = min(file1D->filesize - of1, file2D->filesize - of2);

                        if (diff <= bufSize) {  // indent end
                                file1.moveTo(of1 + diff - steps[cmmMovePage]);
                                file2.moveTo(of2 + diff - steps[cmmMovePage]);

                                return;
                        }

                        of1 += bufSize;
                        of2 += bufSize;
                }

                else if (of1 == file1D->filesize && of2 == file2D->filesize) {
                        file1.moveToEnd();
                        file2.moveToEnd();

                        return;
                }

                SeekFile(fd1, of1);
                SeekFile(fd2, of2);

                of1 -= of2;

                Size size;
                do {
                        Size size1 = ReadFile(fd1, bufFile1, staticSize);
                        Size size2 = ReadFile(fd2, bufFile2, staticSize);

                        size = min(size1, size2);

                        if (size < staticSize || stopRead) {
                                break;
                        }

                        if (memcmp(bufFile1, bufFile2, staticSize)) {
                                break;
                        }

                        of2 += staticSize;  // only one needed

                } while (1);

                Size i=0;
                for (; i < size; i += 16) {
                        Quad q1 = _mm_load_si128((Quad*) (bufFile1 + i)),
                             q2 = _mm_load_si128((Quad*) (bufFile2 + i));

                        q1 = _mm_cmpeq_epi8(q1, q2);

                        if ((_mm_movemask_epi8(q1)) != 0xFFFF) {
                                break;
                        }
                }

                if (i > size) {
                        i = size;
                }

                file1.moveTo(of2 + of1 + i);
                file2.moveTo(of2       + i);
        }

        else {  // downwards
                Size diff1 = 0,
                     diff2 = 0,
                     diff3 = min(of1, of2);

                if (diff3 <= bufSize) {
                        file1.move(-diff3);
                        file2.move(-diff3);

                        return;
                }

                do {
                        of1 -= staticSize;
                        of2 -= staticSize;

                        if (of1 < 0) {
                                diff1 = of1;
                                of1   = 0;
                        }

                        if (of2 < 0) {
                                diff2 = of2;
                                of2   = 0;
                        }

                        SeekFile(fd1, of1);
                        SeekFile(fd2, of2);

                        ReadFile(fd1, bufFile1, staticSize);
                        ReadFile(fd2, bufFile2, staticSize);

                        if (of1 == 0 || of2 == 0 || stopRead) {
                                break;
                        }

                        if (memcmp(bufFile1, bufFile2, staticSize)) {
                                break;
                        }

                } while (1);

                diff1 = staticSize - 1 + diff1;
                diff2 = staticSize - 1 + diff2;

                diff3 = diff1 - diff2;

                if (diff3 > 0) {  // maintain offset
                        of1   = of1 + diff3;
                        diff1 = (Size) bufFile1 + diff3 - 15;
                        diff3 = diff2;
                        diff2 = (Size) bufFile2         - 15;
                }
                else {
                        of2   = of2 - diff3;
                        diff2 = (Size) bufFile2 - diff3 - 15;
                        diff3 = diff1;
                        diff1 = (Size) bufFile1         - 15;
                }

                for (; diff3 >= bufSize; diff3 -= 16) {  // use only one var
                        Quad q1 = _mm_loadu_si128((Quad*) (diff1 + diff3)),
                             q2 = _mm_loadu_si128((Quad*) (diff2 + diff3));

                        q1 = _mm_cmpeq_epi8(q1, q2);

                        if ((_mm_movemask_epi8(q1)) != 0xFFFF) {
                                file1.moveTo(of1 + diff3 - bufSize + 1);
                                file2.moveTo(of2 + diff3 - bufSize + 1);

                                return;
                        }
                }

                file1.moveTo(of1);
                file2.moveTo(of2);
        }
} // end Difference::differ

//====================================================================
// Class FileDisplay member functions
//
// Open a file for display

bool FileDisplay::setFile(char* FileName)
{
        filename = FileName;

        File probe = OpenFile(filename, true);

        if (probe > 0) {
                editable = true;
                close(probe);
        }

        if ((fd = OpenFile(filename)) < 0) {
                return false;
        }

        if ((filesize = SeekFile(fd, 0, SEEK_END)) < 0) {
                return false;
        }

        if (filesize > 68719476736) {  // 2**30*64 == 0x10**9 == 64GB
                sizeTera = true;
        }

        SeekFile(fd, 0);

        return true;
} // end FileDisplay::setFile

void FileDisplay::resizeF()
{
        delete [] dataF;
        delete [] sAllF;

        dataF = new Byte[bufSize];  // max_align_t 0x10
        sAllF = new Byte[bufSize];
}

//--------------------------------------------------------------------
// Set member variables

void FileDisplay::initF(int y, const Difference* Diff)
{
        diffsF = Diff;
        two = y ? true : false;

        cwinF.initW(0, y, screenWidth, numLines + 1, cMainWin);

        resizeF();

        moveTo(startAddr);
}

//--------------------------------------------------------------------
// Display the file contents  ##:disp

void FileDisplay::display()
{
        if (! fd) {
                return;
        }

        short first,
              last,
              row,
              col,
              idx,
              lineLength;

        FPos lineOffset = offset;

        if (scrollOff) {
                diffOffset = scrollOff - offset;
        }
        else if (offset != prevOffset) {
                diffOffset = offset - prevOffset;

                prevOffset = offset;
        }

        Byte pos = (scrollOff ? scrollOff + lineWidth : offset + bufSize)
                        * 100
                        / (filesize > bufSize ? filesize : bufSize);

        char bufStat[screenWidth + 1] = { 0 };
        memset(bufStat, ' ', screenWidth);

        char buf[96],
             buf2[2][48];

        sprintf(buf, " %s %s %d%% %s %s",
                pretty(buf2[0], &offset, 0),
                pretty(buf2[1], &diffOffset, 1),
                pos > 100 ? 100 : pos,
                ignoreCase ? "I" : "i",
                editable ? "RW" : "RO");

        short size_name = screenWidth - strlen(buf),
              size_fname = strlen(filename);

        if (size_fname <= size_name) {
                memcpy(bufStat, filename, size_fname);
        }
        else {
                first = size_name / 4;

                memcpy(bufStat, filename, first);
                memcpy(bufStat + first, " ... ", 5);

                last = size_name - first - 5;

                memcpy(bufStat + first + 5, filename + size_fname - last, last);
        }

        memcpy(bufStat + size_name, buf, strlen(buf));

        cwinF.put(0, 0, bufStat);
        attr(0, 0, cName, strlen(bufStat));

        if (lockState == lockBottom && ! two) {
                attr(0, 0, cHighFile, size_name);
        }
        else if (lockState == lockTop && two) {
                attr(0, 0, cHighFile, size_name);
        }

        if (diffOffset < 0) {
                char *pc = (char*) memchr(buf, '-', strlen(buf));

                attr(size_name + (pc - buf), 0, cMatch, 1);
        }

        char bufHex[screenWidth + 1] = { 0 },
             bufAsc[  lineWidth + 1] = { 0 };

        for (row=0; row < numLines; ++row) {
                memset(bufHex, ' ', screenWidth);
                memset(bufAsc, ' ',   lineWidth);

                if (*(sm4rt + row)) {
                        lineOffset += (lineWidth * (*(sm4rt + row)));
                }

                char *pbufHex = bufHex;

                pbufHex += sprintf(pbufHex, "%0*lX  ", sizeTera ? 12 : 9, lineOffset);

                lineLength = min(lineWidth, dataSize - row * lineWidth);

                for (col = idx = 0; col < lineLength; ++col, ++idx) {
                        Byte b = dataF[row * lineWidth + col];

                        if (! modeAscii) {
                                pbufHex += sprintf(pbufHex, "%02X ", b);
                        }

                        if (isgraph(b)) {
                                bufAsc[idx] = b;
                        }
                        else if (isspace(b)) {
                                bufAsc[idx] = ' ';
                        }
                        else {
                                bufAsc[idx] = modeAscii ? ' ' : '.';
                        }
                }
                *pbufHex = ' ';

                cwinF.put(0, row + 1, bufHex);
                cwinF.put((modeAscii ? leftMar : leftMar2), row + 1, bufAsc);

                for (col=0; col < (sizeTera ? 11 : 8); ++col) {
                        if (*(bufHex + col) != '0') {
                                break;
                        }
                }
                attr(col, row + 1, cAddress, (sizeTera ? 12 : 9) - col);

                if (showRaster) {
                        if (sizeTera) {
                                attr(0, row + 1, cRaster, 1);
                        }
                        attr(sizeTera ? 4 : 1, row + 1, cRaster, 1);
                        attr(sizeTera ? 8 : 5, row + 1, cRaster, 1);
                }

                if (! modeAscii && showRaster && bufHex[leftMar] != ' ') {
                        for (col=0; col <= lineWidth - 8; col += 8) {
                                attr(leftMar  + col * 3 - 1, row + 1, cRaster, 1);
                                attr(leftMar2 + col        , row + 1, cRaster, 1);
                        }
                }

                if (haveDiff) {
                        for (col=0; col < lineWidth; ++col) {
                                if (diffsF->dataD[row * lineWidth + col]) {
                                        attr(leftMar  + col * 3, row + 1, cDiff, 2);
                                        attr(leftMar2 + col    , row + 1, cDiff, 1);
                                }
                        }
                }

                if (se4rchAll) {
                        for (col=0; col < lineWidth; ++col) {
                                if (sAllF[row * lineWidth + col]) {
                                        if (modeAscii) {
                                                attr(leftMar  + col    , row + 1, cMatch, 1);
                                        }
                                        else {
                                                attr(leftMar  + col * 3, row + 1, cMatch, 2);
                                                attr(leftMar2 + col    , row + 1, cMatch, 1);
                                        }
                                }
                        }
                }

                if (se4rch && row >= (searchOff >= searchIndent ? searchIndent / lineWidth : 0)) {
                        for (col=0; se4rch && col < lineWidth; --se4rch, ++col) {
                                if (modeAscii) {
                                        attr(leftMar  + col    , row + 1, cSearch, 1);
                                }
                                else {
                                        attr(leftMar  + col * 3, row + 1, cSearch, 2);
                                        attr(leftMar2 + col    , row + 1, cSearch, 1);
                                }
                        }
                }

                if (seekup) {
                        if (seekup < 0 || row == numLines - 1) {
                                if (modeAscii) {
                                        attr(leftMar , row + 1, cSeek, 1);
                                }
                                else {
                                        attr(leftMar , row + 1, cSeek, 2);
                                        attr(leftMar2, row + 1, cSeek, 1);
                                }

                                seekup = 0;
                        }
                }

                if (*(sm4rt + row)) {
                        for (col=0; col < lineWidth; ++col) {
                                if (modeAscii) {
                                        attr(leftMar  + col    , row + 1, cDiff, 1);
                                }
                                else {
                                        attr(leftMar  + col * 3, row + 1, cDiff, 2);
                                        attr(leftMar2 + col    , row + 1, cDiff, 1);
                                }
                        }
                }

                lineOffset += lineWidth;
        }

        if (se4rchAll) {
                se4rchAll = false;
        }

        updateF();
} // end FileDisplay::display

//--------------------------------------------------------------------
// Busy status

void FileDisplay::busy(bool on=false, bool ic=false, bool np=false)
{
        if (! fd) {
                return;
        }

        if (on) {
                attr(screenWidth - (ic ? 4 : 2),  0, (np ? cHighBus2 : cHighBusy), ic ? 1 : 2);
                updateF();
        }
        else {
                napms(150);
                attr(screenWidth - (ic ? 4 : 2),  0, cName, ic ? 1 : 2);

                if (! singleFile && ! two) {
                        updateF();
                }
        }
}

//--------------------------------------------------------------------
// Display the edit buffer  ##:out

void FileDisplay::editOut(short outOffset)
{
        FPos lineOffset = offset + outOffset;

        char bufHex[screenWidth + 1] = { 0 },
             bufAsc[  lineWidth + 1] = { 0 };

        for (int row=0; row < numLines; ++row) {
                memset(bufHex, ' ', screenWidth);
                memset(bufAsc, ' ',   lineWidth);

                char *pbufHex = bufHex;

                pbufHex += sprintf(bufHex, "%0*lX  ", sizeTera ? 12 : 9, lineOffset);

                int lineLength = min(lineWidth, (int) editBytes.size() - outOffset - row * lineWidth);

                for (int col=0; col < lineLength; ++col) {
                        Byte b = editBytes[outOffset + row * lineWidth + col];

                        pbufHex += sprintf(pbufHex, "%02X ", b);

                        bufAsc[col] = isprint(b) ? b : '.';
                }
                *pbufHex = ' ';

                cwinF.put(0,        row + 1, bufHex);
                cwinF.put(leftMar2, row + 1, bufAsc);

                if (showRaster) {
                        int col[] = { 0, 1, 4, 5, 8 };

                        for (int i = sizeTera ? 0 : 1; i < 5; i += 2) {
                                attr(col[i], row + 1, cRaster, 1);
                        }
                }

                if (showRaster && bufHex[leftMar] != ' ') {
                        for (int col=0; col <= lineWidth - 8; col += 8) {
                                attr(leftMar  + col * 3 - 1, row + 1, cRaster, 1);
                                attr(leftMar2 + col        , row + 1, cRaster, 1);
                        }
                }

                for (int c, col=0; col < lineLength; ++col) {
                        if ((c = editColor[outOffset + row * lineWidth + col])) {
                                attr(leftMar  + col * 3, row + 1, (Style) c, 2);
                                attr(leftMar2 + col    , row + 1, (Style) c, 1);
                        }
                }

                lineOffset += lineWidth;
        }
} // end FileDisplay::editOut

//--------------------------------------------------------------------
// Obtain confirmation for lengthy write

bool FileDisplay::assure()
{
        bool ret = true;
        Size diff = filesize - offset;

        if (diff > warnResize) {
                char str[96],
                     inp[4];

                sprintf(str, " About to write *non-interruptable* %.1fGB!? {yes|no}: ", (double) diff / 1073741824);

                echo();
                for (;;) {
                        positionInWin(two ? cmgGotoBottom : cmgGotoTop, 1+ strlen(str) +5+1, " Attention! ", 5);

                        mvwaddstr(winInput, 2, 1, str);

                        wgetnstr(winInput, inp, 3);

                        if (! strcmp(inp, "yes")) {
                                break;
                        }

                        else if (! strcmp(inp, "no")) {
                                ret = false;
                                break;
                        }
                }
                noecho();
        }
        updateF();
        hideCursor();

        return ret;
} // end FileDisplay::assure

//--------------------------------------------------------------------
// Progress bar single

void FileDisplay::progress1()
{
        int blocks = 25,
            delay  = 4;

        hideCursor();
        positionInWin(two ? cmgGotoBottom : cmgGotoTop, 2+ blocks +2, "");

        wchar_t bar[blocks + 1];
        memset(bar, 0, sizeof(bar));

        for (int i=0; i < blocks; ++i) {
                for (int j=0; j < 8; ++j) {
                        bar[i] = barSyms[j];

                        mvwaddwstr(winInput, 1, 2, bar);
                        wrefresh(winInput);
                        napms(delay);
                }
        }
        napms(250);
}

//--------------------------------------------------------------------
// Progress bar multiple

void FileDisplay::progress(wchar_t* bar, int count, int delay=0, int stint=1)
{
        int pos = count * stint / 8,
            sym = count * stint % 8;

        for (int i=0; i < stint; ++i) {
                bar[pos] = barSyms[sym % 8];

                if (! (++sym % 8)) {
                        ++pos;
                }

                mvwaddwstr(winInput, 1, 2, bar);
                wrefresh(winInput);

                if (delay) {
                        napms(delay);
                }
        }
}

//--------------------------------------------------------------------
// Append the remainder

bool FileDisplay::WriteTail(FPos start)
{
        bool insert = start > 0 ? true : false;

        FPos srcOff = offset + dataSize,
             dstOff = offset + start * (insert ? 1 : -1);
        Size remain = filesize - srcOff;

        int width = (screenWidth - 4) * 8,
            level = (screenWidth / 3) * 8,
            cargo = staticSize,
            loops = mCeil(remain, cargo),
            multi = 1,
            scale = 0,
            count = 0,
            stage = 0,
            round = 0,
            final = 0;

        if (remain <= cargo) {
                progress1();
                multi = 0;
        }

        else if (loops > width) {
                scale = mCeil(loops, width);
                width = mCeil(loops, scale);
        }

        else if (loops > level) {
                width = loops;
        }

        else {
                cargo = remain / level >> 12;
                cargo = cargo ? cargo : 1;
                cargo *= 4096;  // page align

                width = mCeil(remain, cargo);
        }

        stage = width - 8;

        width = mCeil(width, 8);

        wchar_t bar[width + 1];
        memset(bar, 0, sizeof(bar));

#if SHOW_WRITE_SUMMARY
        int term = time(NULL);
#endif
        if (multi) {
                Size laptime = 0,
                     laps    = timer();

                positionInWin(two ? cmgGotoBottom : cmgGotoTop, 2+ width +2, "");

                if (insert) {
                        srcOff = filesize;  // downwards
                        dstOff = filesize + start - dataSize;
                }

                for (; remain > cargo; ++count) {  // two cases - one loop
                        if (insert) {
                                srcOff -= cargo;
                        }
                        SeekFile(fd, srcOff);
                        ReadFile(fd, buffer, cargo);

                        if (insert) {
                                dstOff -= cargo;
                        }
                        else {
                                srcOff += cargo;
                        }

                        SeekFile(fd, dstOff);
                        if (! WriteFile(fd, buffer, cargo)) {
                                return false;
                        }
                        if (! insert) {
                                dstOff += cargo;
                        }

                        remain -= cargo;

                        if (scale && count % scale) {
                                continue;
                        }

                        if (mScale >= stage) {
                                if (mScale == stage) {
                                        laptime = timer();
                                }
                                else {
                                        final = timer(3, laptime) / ++round;
                                }
                        }

                        progress(bar, mScale);

                        laps = timer(3, laps);

                        if (barDelay > laps) {
                                napms(barDelay - laps);
                        }

                        laps = timer();
                }

                if (insert) {
                        srcOff -= remain;
                        dstOff -= remain;
                }
        }

        if (remain > 0) {
                SeekFile(fd, srcOff);
                ReadFile(fd, buffer, remain);

                SeekFile(fd, dstOff);
                if (! WriteFile(fd, buffer, remain)) {
                        return false;
                }
        }

        if (! insert && ftruncate(fd, dstOff + remain) == ERR) {
                return false;
        }

#if SHOW_WRITE_SUMMARY
        if (filesize - offset > warnResize) {
                term = time(NULL) - term;

                sprintf(bufTimer, "  %dsec (%.1fmin)  %ldMByte/s  ",
                        term,
                        (float) term / 60,
                        (filesize - offset) / 1048576 / (term ? term : 1));
        }
#endif
        if (multi) {
                for (;mScale < width * 8; ++count) {
                        if (scale && count % scale) {
                                continue;
                        }

                        progress(bar, mScale, final);  // neat finish
                }

                napms(400);
        }

        return true;
} // end FileDisplay::WriteTail

//--------------------------------------------------------------------
// Edit the file  ##:edit

void FileDisplay::edit(const FileDisplay* other)
{
        if (! editable) {
                return;
        }

        bool hiNib   = true,
             ascii   = false,
             changed = false;

        short x = 0,
              y = 0,
              outOffset;

        int cur,
            endY,
            endX,
            key;

        editBytes.clear();
        editColor.clear();

        for (int i=0; i < dataSize; ++i) {
                editBytes.push_back(dataF[i]);
                editColor.push_back(0);
        }

        cwinF.setCursor(leftMar, 1);
        showCursor();

        for (;;) {
                endY = editBytes.size() ? (editBytes.size() - 1) / lineWidth : 0;
                endX = editBytes.size() ? (editBytes.size() - 1) % lineWidth : 0;

                if (y > endY) {
                        y = endY;
                        x = endX;
                }

                if (y == endY && x > endX) {
                        x = endX;
                }

                cur = y * lineWidth + x;

                outOffset = cur >= bufSize ? (cur - bufSize) / lineWidth + 1 : 0;

                editOut(outOffset * lineWidth);

                cwinF.setCursor((ascii ? leftMar2 + x : leftMar + 3 * x + ! hiNib), y - outOffset + 1);

                key = readKeyF();

                switch (key) {
                        case KEY_ESCAPE:
                                goto done;

                        case KEY_TAB:
                                hiNib  = true;
                                ascii ^= true;
                                break;

                        case KEY_IC:
                                changed = true;

                                editBytes.insert(editBytes.begin() + cur, ascii ? ' ' : '\0');
                                editColor.insert(editColor.begin() + cur, cInsert);
                                break;

                        case KEY_DC:
                                if (editBytes.size()) {
                                        changed = true;

                                        editBytes.erase(editBytes.begin() + cur);
                                        editColor.erase(editColor.begin() + cur);
                                }
                                break;

                        case KEY_HOME:
                                y = x = 0;
                                break;

                        case KEY_END:
                                y = endY;
                                x = endX;
                                break;

                        case KEY_LEFT:
                                if (! hiNib) {
                                        hiNib = true;
                                        break;
                                }

                                else {
                                        if (! ascii) {
                                                hiNib = false;
                                        }

                                        if (--x < 0) {
                                                x = y ? lineWidth - 1 : endX;
                                        }
                                        else {
                                                break;
                                        }
                                }  // fall thru

                        case KEY_UP:
                                if (--y < 0) {
                                        y = endY;

                                        if (x > endX) {
                                                --y;
                                        }
                                }
                                break;

                        default: {
                                short newByte = -1;

                                if (key == KEY_RETURN && other && other->dataSize > (cur - outOffset * lineWidth)) {
                                        newByte = other->dataF[cur - outOffset * lineWidth];

                                        hiNib = false;  // advance
                                }

                                else if (ascii && isprint(key)) {
                                        newByte = key;
                                }

                                else {
                                        if (isxdigit(key)) {
                                                newByte = upCase(key) - (isdigit(key) ? 48 : 55);

                                                if (hiNib) {
                                                        newByte <<= 4;
                                                }

                                                newByte |= editBytes[cur] & (hiNib ? 0x0F : 0xF0);
                                        }
                                }

                                if (newByte < 0) {
                                        break;
                                }

                                changed = true;

                                editBytes[cur] = newByte;

                                editColor[cur] = (cur < dataSize && dataF[cur] == newByte) ? 0 : cEdit;
                        }  // fall thru

                        case KEY_RIGHT:
                                if (hiNib && ! ascii) {
                                        hiNib = false;
                                        break;
                                }

                                hiNib = true;

                                if (++x == lineWidth) {
                                        x = 0;
                                }

                                if (y == endY && x > endX) {
                                        x = 0;
                                }

                                if (x) {
                                        break;
                                }  // fall thru

                        case KEY_DOWN:
                                if (++y > endY) {
                                        y = 0;
                                }

                                if (y == endY && x > endX) {
                                        y = 0;
                                }
                }
        }

done:
        if (changed) {
                changed = false;

                int size = editBytes.size();
                Byte buf[size];

                for (int i=0; i < size; ++i) {
                        buf[i] = editBytes[i];
                }

                if (size == dataSize) {
                        if (! memcmp(buf, dataF, size)) {
                                goto done;
                        }
                }

                if (! sizeTera && filesize + size - dataSize > 68719476736) {  // very special case
                        hideCursor();
                        positionInWin(two ? cmgGotoBottom : cmgGotoTop, 1+ 14 +1, "", 5);

                        mvwaddstr(winInput, 2, 1, "  File >64GB  ");
                        wgetch(winInput);
                        goto done;
                }

                positionInWin(two ? cmgGotoBottom : cmgGotoTop, 1+ 19 +3+1, "");

                mvwaddstr(winInput, 1, 1, " Save changes [y]: ");

                key = wgetch(winInput);

                if (upCase(key) != 'Y') {
                        goto done;
                }

                wechochar(winInput, key);
                napms(500);

                bool ret = false;

                close(fd);
                fd = OpenFile(filename, true);

                SeekFile(fd, offset);

                if (size == dataSize) {
                        ret = WriteFile(fd, buf, dataSize);

                        progress1();
                }

                else if (size < dataSize) {
                        if (assure()) {
                                if (WriteFile(fd, buf, size)) {
                                        ret = WriteTail(size * -1);
                                }
                        }
                }

                else {  // size > dataSize
                        if (assure()) {
                                SeekFile(fd, 0, SEEK_END);

                                if (WriteFile(fd, buffer, size - dataSize)) {  // check
                                        if (WriteTail(size)) {
                                                SeekFile(fd, offset);

                                                ret = WriteFile(fd, buf, size);
                                        }
                                }
                        }
                }

                if (ret) {
                        if (fsync(fd) == OK) {
                                if (close(fd) == ERR) {  // seamless error tracking
                                        ret = false;
                                }

                                fd = -1;
                        }
                        else {
                                ret = false;
                        }
                }

                if (fd > 0) {
                        close(fd);
                }

                fd = OpenFile(filename);

                filesize = SeekFile(fd, 0, SEEK_END);

                move(0);

                updateF();

                if (ret) {
                        positionInWin(two ? cmgGotoBottom : cmgGotoTop,
                                1+ (*bufTimer ? strlen(bufTimer) : 11) +1, "", *bufTimer ? 7 : 5);

                        mvwaddstr(winInput, 2, *bufTimer ? (strlen(bufTimer) - 11) / 2 + 1 : 1, "  Success  ");

                        if (*bufTimer) {
                                mvwaddstr(winInput, 4, 1, bufTimer);
                                wgetch(winInput);

                                *bufTimer = 0;
                        }
                        else {
                                wrefresh(winInput);
                                napms(900);
                        }
                }

                else {
                        positionInWin(two ? cmgGotoBottom : cmgGotoTop, 1+ 11 +1, "", 5);

                        mvwaddstr(winInput, 2, 1, "  Failed!  ");
                        wgetch(winInput);
                }
        }

        else {
                hideCursor();
        }
} // end FileDisplay::edit

//--------------------------------------------------------------------
// Jump a specific percentage forward / backward

void FileDisplay::skip(bool upwards=false)
{
        FPos step = filesize / 100;

        if (upwards) {
                move(step * -skipBack);
        }
        else {
                move(step * skipForw);
        }
}

//--------------------------------------------------------------------
// Synchronize the plains
//
// '1' | upper:  sync file1 with file2
// '2' | lower:  sync file2 with file1

void FileDisplay::sync(const FileDisplay* other)
{
        if (other->dataSize) {
                moveTo(other->offset);
        }
        else {
                moveToEnd();
        }
}

//--------------------------------------------------------------------
// Mark all search strings

void FileDisplay::mark(Byte* searchFor, Size searchLen)
{
        Byte ign[dataSize];

        Byte* src = ignoreCase ? ign : dataF;

        if (ignoreCase) {
                memcpy(ign, dataF, dataSize);

                lowCase(ign, dataSize);
        }

        memset(sAllF, 0, bufSize);

        for (int i=0; i < dataSize; ++i) {
                Byte* p;

                if ((p = (Byte*) memmem(src + i, dataSize - i, searchFor, searchLen))) {
                        for (int j=0; j < searchLen; ++j) {
                                *(sAllF + (p - src) + j) = true;
                        }

                        i += searchLen - 1;
                }
        }

        se4rchAll = true;
}

//--------------------------------------------------------------------
// Change the file position  ##:to

void FileDisplay::moveTo(FPos newOffset)
{
        if (newOffset < 0) {
                offset = 0;
        }
        else if (newOffset > filesize) {
                offset = filesize;
        }
        else {
                offset = newOffset;
        }

        SeekFile(fd, offset);

        dataSize = ReadFile(fd, dataF, bufSize);
}

//--------------------------------------------------------------------
// Change the file position by searching

void FileDisplay::moveForw(Byte* searchFor, Size searchLen)
{
        FPos newPos = searchOff > 0 ? searchOff + 1 : (searchOff < 0 ? 1 : offset);

        Size bias = 0,
             hint = 0;

        Quad lead = _mm_setzero_si128(),
             mask = _mm_set1_epi8(0xFF);

        for (Full i=0; i < (Full) searchLen % 16; ++i) {  // shl128 only with immediate
                mask = _mm_bslli_si128(mask, 1);
        }

        while (! *(searchFor + bias) && bias < searchLen) {
                ++bias;
        }

        if (bias == searchLen) {
                bias = 0;
        }
        else {
                lead = _mm_set1_epi8(*(searchFor + bias));
                hint = 1;
        }

        for (; ; newPos += staticSize - searchLen + 1) {
                SeekFile(fd, newPos);

                Size bytesRead = ReadFile(fd, buffer, staticSize);

                if (bytesRead < searchLen || stopRead) {
                        break;
                }

                if (ignoreCase) {
                        lowCase(buffer, bytesRead);
                }

                for (Size i=0, todo=0, c; i <= bytesRead - searchLen; i += 16) {
                        Quad turbo = _mm_loadu_si128((Quad*) (buffer + i + bias)),
                             zero  = _mm_setzero_si128(),
                             temp  = _mm_cmpeq_epi8(turbo, zero);

                        if (_mm_movemask_epi8(temp) == 0xFFFF) {
                                if (hint) {
                                        continue;
                                }
                        }

                        else {
                                temp = _mm_cmpeq_epi8(turbo, lead);
                        }

                        if (! (todo = _mm_movemask_epi8(temp))) {
                                continue;
                        }

                        do {
                                c = i + _bit_scan_forward(todo);

                                if (searchFor[searchLen - 1] == buffer[c + searchLen - 1]) {
                                        Size j = 0;
                                        Quad s,
                                             b;

                                        do {
                                                s = _mm_loadu_si128((Quad*) (searchFor  + j));
                                                b = _mm_loadu_si128((Quad*) (buffer + c + j));

                                                if (searchLen < j + 16) {
                                                        break;
                                                }

                                                temp = _mm_cmpeq_epi8(s, b);

                                                if (_mm_movemask_epi8(temp) != 0xFFFF) {
                                                        goto next;
                                                }

                                        } while (j += 16);

                                        temp = _mm_cmpeq_epi8(s, b);
                                        temp = _mm_or_si128(temp, mask);

                                        if (_mm_movemask_epi8(temp) != 0xFFFF) {
                                                goto next;
                                        }

                                        if (c <= bytesRead - searchLen) {  // limit turbo
                                                newPos    = newPos + c;
                                                searchOff = newPos ? newPos : -1;  // tri-state
                                                se4rch    = searchLen;

                                                moveTo(newPos - (searchOff >= searchIndent ? searchIndent : 0));

                                                mark(searchFor, searchLen);
                                                return;
                                        }
                                }
next:
                                asm ("btr{q %1, %0 | %0, %1}" : "+r" (todo) : "r" (c - i));  // use full line

                        } while (todo);
                }
        }

        searchOff = 0;

        moveTo(stopRead ? newPos : filesize);
} // end FileDisplay::moveForw

//--------------------------------------------------------------------
// Change the file position by searching backwards

void FileDisplay::moveBack(Byte* searchFor, Size searchLen)
{
        FPos newPos = searchOff > 0 ? searchOff : offset;

        Size bias = 0,
             hint = 0,
             diff = 0;

        Quad lead = _mm_setzero_si128(),
             mask = _mm_set1_epi8(0xFF);

        for (Full i=0; i < (Full) searchLen % 16; ++i) {
                mask = _mm_bslli_si128(mask, 1);
        }

        while (! *(searchFor + bias) && bias < searchLen) {
                ++bias;
        }

        if (bias == searchLen) {
                bias = 0;
        }
        else {
                lead = _mm_set1_epi8(*(searchFor + bias));
                hint = 1;
        }

        if (newPos + searchLen - 1 > filesize) {
                newPos = filesize - searchLen + 1;
        }

        for (;;) {
                newPos -= (staticSize - searchLen + 1);

                if (newPos < 0) {
                        diff   = newPos;
                        newPos = 0;
                }

                SeekFile(fd, newPos);

                Size bytesRead = ReadFile(fd, buffer, staticSize);

                if (ignoreCase) {
                        lowCase(buffer, bytesRead);
                }

                for (Size i = staticSize - searchLen + diff, todo=0, c; i >= 0; i -= 16) {
                        Quad turbo = _mm_loadu_si128((Quad*) (buffer + i + bias - 15)),
                             zero  = _mm_setzero_si128(),
                             temp  = _mm_cmpeq_epi8(turbo, zero);

                        if (_mm_movemask_epi8(temp) == 0xFFFF) {
                                if (hint) {
                                        continue;
                                }
                        }

                        else {
                                temp = _mm_cmpeq_epi8(turbo, lead);
                        }

                        if (! (todo = _mm_movemask_epi8(temp))) {
                                continue;
                        }

                        do {
                                c = i - 15 + _bit_scan_reverse(todo);

                                if (searchFor[searchLen - 1] == buffer[c + searchLen - 1]) {
                                        Size j = 0;
                                        Quad s,
                                             b;

                                        do {
                                                s = _mm_loadu_si128((Quad*) (searchFor  + j));
                                                b = _mm_loadu_si128((Quad*) (buffer + c + j));

                                                if (searchLen < j + 16) {
                                                        break;
                                                }

                                                temp = _mm_cmpeq_epi8(s, b);

                                                if (_mm_movemask_epi8(temp) != 0xFFFF) {
                                                        goto next;
                                                }

                                        } while (j += 16);

                                        temp = _mm_cmpeq_epi8(s, b);
                                        temp = _mm_or_si128(temp, mask);

                                        if (_mm_movemask_epi8(temp) != 0xFFFF) {
                                                goto next;
                                        }

                                        if (c >= 0) {
                                                newPos    = newPos + c;
                                                searchOff = newPos ? newPos : -1;
                                                se4rch    = searchLen;

                                                moveTo(newPos - (searchOff >= searchIndent ? searchIndent : 0));

                                                mark(searchFor, searchLen);
                                                return;
                                        }
                                }
next:
                                asm ("btr{q %1, %0 | %0, %1}" : "+r" (todo) : "r" (c - i + 15));

                        } while (todo);
                }

                if (! newPos || stopRead) {
                        break;
                }
        }

        searchOff = 0;

        moveTo(newPos);
} // end FileDisplay::moveBack

//--------------------------------------------------------------------
// Seek forward to next byte not equal to current head + bufSize

void FileDisplay::seekForw()
{
        Byte *pTakeThat = dataF + dataSize - 1,
              searchFor = modeAscii && ! isprint(*pTakeThat) ? ' ' : *pTakeThat;

        Quad lead = _mm_set1_epi8(searchFor);

        FPos newPos = offset + dataSize;

        Size here = -1;

        for (Size bytesRead; ; newPos += staticSize) {
                SeekFile(fd, newPos);

                bytesRead = ReadFile(fd, buffer, staticSize);

                if (bytesRead <= 0 || stopRead) {
                        break;
                }

                if (modeAscii) {
                        setAscii(bytesRead);
                }

                for (Size i=0, m; i < bytesRead; i += 16) {
                        Quad turbo = _mm_load_si128((Quad*) (buffer + i));  // aligned

                             turbo = _mm_cmpeq_epi8(turbo, lead);

                        if ((m = _mm_movemask_epi8(turbo)) != 0xFFFF) {
                                here = i + _bit_scan_forward((Word) ~m);

                                goto done;
                        }
                }
        }
done:
        if (here >= 0) {
                seekup = -1;

                moveTo(newPos + here);
        }

        else if (stopRead) {
                moveTo(newPos);
        }

        else {
                moveToEnd();
        }
} // end FileDisplay::seekForw

//--------------------------------------------------------------------
// Seek backward to next byte not equal to current head

void FileDisplay::seekBack()
{
        Byte searchFor = modeAscii && ! isprint(*dataF) ? ' ' : *dataF;

        Quad lead = _mm_set1_epi8(searchFor);

        FPos newPos = offset - staticSize;

        Size diff =  0,
             here = -1;

        for (Size bytesRead; ; newPos -= staticSize) {
                if (newPos < 0) {
                        diff   = newPos;
                        newPos = 0;
                }

                SeekFile(fd, newPos);

                if ((bytesRead = ReadFile(fd, buffer, staticSize)) <= 0) {
                        break;
                }

                if (modeAscii) {
                        setAscii(bytesRead);
                }

                for (Size i = staticSize - 1 + diff, m; i >= 0; i -= 16) {
                        Quad turbo = _mm_loadu_si128((Quad*) (buffer + i - 15));

                             turbo = _mm_cmpeq_epi8(turbo, lead);

                        if ((m = _mm_movemask_epi8(turbo)) != 0xFFFF) {
                                here = i + _bit_scan_reverse((Word) ~m) - 15;

                                goto done;
                        }
                }

                if (! newPos || stopRead) {
                        break;
                }
        }
done:
        if (here >= 0) {
                newPos += here - steps[cmmMovePage];

                if (newPos >= 0) {
                        seekup = 1;
                }

                moveTo(newPos);
        }

        else if (stopRead) {
                moveTo(newPos);
        }

        else {
                moveTo(0);
        }
} // end FileDisplay::seekBack

//--------------------------------------------------------------------
// Scroll forward with skipping same content lines

void FileDisplay::smartScroll()
{
        bool (*pCmp) (Byte*, Byte*, Full) = lineWidth == 24 ? cmpLineU : cmpLine;

        FPos newPos = scrollOff;

        if (! newPos) {  // advance
                newPos = offset & ~0xF;

                if (filesize - newPos > steps[cmmMovePage]) {
                        moveTo(newPos);

                        newPos += steps[cmmMovePage];

                        Size end = (Size) dataF + (numLines - 1) * lineWidth;

                        for (Byte* p = dataF; (Size) p < end; p += lineWidth) {  // scroll if possible
                                if (! pCmp(p, p + lineWidth, lineWidth)) {
                                        newPos -= steps[cmmMovePage];
                                        break;
                                }
                        }
                }
        }

        if (filesize - newPos <= steps[cmmMovePage]) {
                moveToEnd();

                scrollOff = 0;
                return;
        }

        offset = newPos;

        SeekFile(fd, newPos);

        Size bytesRead = ReadFile(fd, buffer, staticSize);

        if (modeAscii) {
                setAscii(bytesRead);
        }

        FPos repeat = 0;

        memcpy(dataF, buffer, lineWidth);
        bytesRead -= lineWidth;

        Size i = 1, j = 1;
        for (; bytesRead > 0;) {
                if (pCmp(dataF + (i - 1) * lineWidth, buffer + j * lineWidth, lineWidth)) {
                        memcpy(dataF + i * lineWidth, buffer + j * lineWidth, lineWidth);

                        if (repeat) {
                                *(sm4rt + i) = repeat;
                                repeat = 0;
                        }

                        if (i == numLines - 1) {
                                break;
                        }

                        ++i;
                }

                else {
                        ++repeat;
                }

                bytesRead -= lineWidth;
                ++j;

                if (bytesRead < lineWidth) {
                        newPos += j * lineWidth;
                        j = 0;

                        SeekFile(fd, newPos);

                        bytesRead = ReadFile(fd, buffer, staticSize);

                        if (modeAscii) {
                                setAscii(bytesRead);
                        }

                        if (bytesRead < lineWidth || stopRead) {
                                if (bytesRead > 0) {
                                        *(sm4rt + i) = repeat;

                                        memcpy(dataF + i * lineWidth, buffer, min(bytesRead, lineWidth));
                                }

                                else {
                                        if (repeat) {
                                                *(sm4rt + i) = --repeat;

                                                memcpy(dataF + i * lineWidth, dataF + (i - 1) * lineWidth, lineWidth);
                                                ++i;  // tricky!
                                        }
                                }

                                break;
                        }
                }
        }

        scrollOff = newPos + j * lineWidth;

        dataSize = i * lineWidth + min(bytesRead, lineWidth);
} // end FileDisplay::smartScroll

//====================================================================
// Class InputManager

class InputManager
{
    private:
        char           *buf;
        const char     *restrictChar;
        StrDeq         &history;
        size_t          historyPos;
        string          historyInp;
        int             maxLen;
        int             step;
        int             len = 0;
        int             cur = 0;
        bool            upcase;
        bool            splitHex;
        bool            overStrike = false;

    private:
        void    useHistory(int delta);

    public:
                InputManager(char* Buf, int MaxLen, StrDeq& History):
                        buf(Buf), history(History), historyPos(History.size()), maxLen(MaxLen) {}
               ~InputManager()                          {}

        void    setCharacters(const char* RestrictChar) { restrictChar = RestrictChar; }
        void    setSplitHex(bool SplitHex)              { splitHex = SplitHex; }
        void    setUpcase(bool Upcase)                  { upcase = Upcase; }
        void    setStep(int Step)                       { step = Step; }

        void    run();
}; // end InputManager

//====================================================================
// Class InputManager member functions

//--------------------------------------------------------------------
// Switch the current input line with one from the history

void InputManager::useHistory(int delta)
{
        if (historyPos == history.size()) {
                historyInp.assign(buf, len);
        }

        historyPos += delta;

        string s = historyPos == history.size() ? historyInp : history[historyPos];

        cur = len = s.size();

        memset(buf, ' ', maxLen);

        memcpy(buf, s.data(), len);
}

//--------------------------------------------------------------------
// Run the main loop to get an input string  ##:run

void InputManager::run()
{
        memset(buf, ' ', maxLen);
        buf[maxLen] = 0;

        showCursor();

        for (;;) {
                mvwaddstr(winInput, 1, 2, buf);
                wmove(winInput, 1, 2 + cur);

                int key = wgetch(winInput);

                if (upcase) {
                        key = upCase(key);
                }

                if (isprint(key)) {
                        if (restrictChar && ! strchr(restrictChar, key)) {
                                continue;
                        }

                        if (overStrike) {
                                if (cur >= maxLen) {
                                        continue;
                                }
                        }

                        else {
                                if (! (cur % step)) {
                                        if (len + step > maxLen) {
                                                continue;
                                        }

                                        if (cur != len) {  // true insert
                                                memmove(buf + cur + step, buf + cur, len - cur);

                                                len += step;

                                                if (splitHex) {
                                                        buf[cur + 1] = ' ';
                                                }
                                        }
                                }
                        }

                        mEdit

                        buf[cur++] = key;

                        if (splitHex && cur % 3 == 2) {
                                ++cur;
                        }

                        if (cur > len) {
                                len = cur;
                        }
                }

                else {
                        if (key == KEY_IC) {
                                overStrike ^= true;
                                showCursor(overStrike);
                                continue;
                        }

                        if (splitHex && cur) {  // normalize
                                if (buf[cur] == ' ' && buf[cur - 1] != ' ') {
                                        buf[cur]     = buf[cur - 1];
                                        buf[cur - 1] = '0';

                                        if (cur == len) {
                                                len += 2;
                                        }
                                }

                                cur -= cur % step;
                        }

                        switch (key) {
                                case KEY_ESCAPE:
                                case KEY_RETURN:
                                        buf[key == KEY_RETURN ? len : 0] = 0;
                                        goto done;

                                case KEY_LEFT:
                                case KEY_RIGHT:
                                        if (key == KEY_LEFT ? cur : cur < len) {
                                                cur = cur + (key == KEY_LEFT ? -step : step);
                                        }
                                        break;

                                case KEY_HOME:
                                case KEY_END:
                                        cur = key == KEY_END ? len : 0;
                                        break;

                                case KEY_UP:
                                        if (historyPos) {
                                                useHistory(-1);
                                        }
                                        break;

                                case KEY_DOWN:
                                        if (historyPos < history.size()) {
                                                useHistory(+1);
                                        }
                                        break;

                                case KEY_DC:
                                        if (cur >= len) {
                                                continue;
                                        }

                                        mEdit

                                        memmove(buf + cur, buf + cur + step, len - cur - step);
                                        memset(buf + len - step, ' ', step);

                                        len -= step;
                                        break;

                                case KEY_BACKSPACE:
                                        if (! cur) {
                                                continue;
                                        }

                                        mEdit

                                        memmove(buf + cur - step, buf + cur, len - cur);
                                        memset(buf + len - step, ' ', step);

                                        cur -= step;
                                        len -= step;
                                        break;

                                case KEY_CTRL_U:  // unix-line-discard
                                        mEdit

                                        memmove(buf, buf + cur, len - cur);
                                        memset(buf + len - cur, ' ', cur);

                                        len -= cur;
                                        cur = 0;
                                        break;

                                case KEY_CTRL_K:  // kill-line
                                        mEdit

                                        memset(buf + cur, ' ', len - cur);
                                        len = cur;
                        }
                }
        }

done:
        hideCursor();

        if (*buf) {
                for (auto exists = history.begin(); exists != history.end(); ++exists) {
                        if (*exists == buf) {
                                history.erase(exists);
                                break;
                        }
                }

                if (history.size() == maxHistory) {
                        history.pop_front();
                }

                history.push_back(buf);
        }

        return;
} // end InputManager::run

//====================================================================
// Global Functions which uses Objects

//--------------------------------------------------------------------
// Get a string using InputManager

void getString(char* buf, int maxlen, StrDeq& history,
                        const char* restrictChar=NULL, bool upcase=false, bool splitHex=false)
{
        InputManager manager(buf, maxlen, history);

        manager.setCharacters(restrictChar);
        manager.setSplitHex(splitHex);
        manager.setUpcase(upcase);
        manager.setStep(splitHex ? 3 : 1);

        manager.run();
}

//--------------------------------------------------------------------
// Program setup  ##:s

void setup()
{
        calcScreenLayout();  // global vars

        if (__builtin_cpu_supports("sse2")) {  // sse2 is default on
                useSSE2 = true;
        }

        if (! (winInput = newwin(3, inWidth, 0, 0))) {
                exitMsg(22, "Failed to create input window.");
        }
        keypad(winInput, true);

        if (! (winHelp = newwin(helpHeight, helpWidth,
                              1 + (linesTotal - helpHeight) / 3,
                              1 + (screenWidth - helpWidth) / 2))) {
                exitMsg(23, "Failed to create help window.");
        }

        wbkgd(winHelp, attribStyle[cHelpWin]);
        box(winHelp, 0, 0);

        mvwaddstr(winHelp,              0, (helpWidth - 6)                   / 2, " Help ");
        mvwaddstr(winHelp, helpHeight - 1, (helpWidth - strlen(helpVersion)) / 2, helpVersion);
        mvwaddstr(winHelp, helpHeight - 1, (helpWidth - 8)                      , useSSE2 ? "SSE2" : "");

        for (int i=0; i < helpHeight - 2; ++i) {  // exclude border
                mvwaddstr(winHelp, i + 1, 1, aHelp[i]);
        }

        for (int i=0; aBold[i]; i += 2) {
                mvwchgat(winHelp, aBold[i], aBold[i + 1], 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);
        }

        if (! singleFile) {
                diffs.resizeD();
        }

        sm4rt = (FPos*) calloc(numLines, sizeof(FPos));

        file1.initF(0, (singleFile ? NULL : &diffs));

        if (! singleFile) {
                file2.initF(numLines + 1, &diffs);
        }
} // end setup

//--------------------------------------------------------------------
// Test progress bar

void ee()
{
        for (int blocks = 25, naps = 4, go = 0; ;) {
                for (int i=1; i;) {
                        char buf[32];
                        sprintf(buf, " %d %d ", blocks, naps);
                        positionInWin(cmgGotoTop, 2+ blocks +2, buf);

                        if (! go++) break;

                        flushinp();
                        int key = wgetch(winInput);

                        switch (key) {
                                case KEY_UP:      if (naps < 50) ++naps; break;
                                case KEY_DOWN:    if (naps >  0) --naps; break;

                                case KEY_LEFT:    if (blocks > 3) --blocks; file1.updateF(); break;
                                case KEY_RIGHT:   if (blocks < screenWidth - 4) ++blocks;    break;

                                case KEY_ESCAPE:  file1.updateF(); return;
                                default:          i--;
                        }
                }

                wchar_t bar[blocks + 1];
                memset(bar, 0, sizeof(bar));

                for (int i=0; i < blocks; ++i) {
                        for (int j=0; j < 8; ++j) {
                                bar[i] = barSyms[j];

                                mvwaddwstr(winInput, 1, 2, bar);
                                wrefresh(winInput);
                                napms(naps);
                        }
                }
                napms(200);
        }
}

//--------------------------------------------------------------------
// Process cmdline arguments

void processArgs(int argc, char** argv)
{
        if (*argv[2] == '-') {
                dumpMode = 1;

                if (*++argv[2] == '-') {
                        dumpMode = 2;
                }

                if (argv[3]) {
                        Size tmp = strtol(argv[3], NULL, 0);

                        if (strchr(argv[3], 'l')) {
                                dumpLen = tmp;
                        }
                        else if (strchr(argv[3], 'w')) {
                                dumpWid = tmp;
                        }
                        else {
                                dumpBeg = tmp;
                        }

                        if (argv[4]) {
                                tmp = strtol(argv[4], NULL, 0);

                                if (strchr(argv[4], 'l')) {
                                        dumpLen = tmp;
                                }
                                else if (strchr(argv[4], 'w')) {
                                        dumpWid = tmp;
                                }
                                else {
                                        dumpEnd = tmp;
                                }

                                if (argv[5]) {
                                        dumpWid = strtol(argv[5], NULL, 0);
                                }
                        }
                }

                if (dumpWid <= 0) {
                        dumpWid = dumpDef;
                }
        }

        else if (argv[3] && *argv[3] == '-') {
                if (*++argv[3] == '-') {
                        diffMode = 2;
                }
                else {
                        diffMode = 1;
                }

                singleFile = false;
        }

        else {
                File probe = OpenFile(argv[2]);

                if (probe > 0) {
                        singleFile = false;

                        close(probe);
                }
                else {
                        file1.startAddr = strtol(argv[2], NULL, 0);

                        if (! file1.startAddr) {
                                singleFile = false;  // error
                        }
                }

                if (! singleFile && argv[3]) {
                        file1.startAddr = strtol(argv[3], NULL, 0);

                        if (argv[4]) {
                                file2.startAddr = strtol(argv[4], NULL, 0);
                        }
                        else {
                                file2.startAddr = file1.startAddr;
                        }
                }
        }
} // end processArgs

//--------------------------------------------------------------------
// Get a file position and move there  ##:p

void gotoPosition(Command cmd)
{
        positionInWin(cmd, inWidth + 1 + 4, " Goto ");  // cursor + border

        char buf[inWidth + 1];

        getString(buf, inWidth, positionHistory, hexDigitsGoto);

        if (! buf[0]) {
                return;
        }

        int rel = 0;

        if (*buf == '+') {
                ++rel;
        }

        if (*buf == '-') {
                --rel;
        }

        if (rel) {
                *buf = ' ';
        }

        FPos pos1 = 0,
             pos2 = 0;

        if (strchr(buf, '%')) {
                int i = atoi(buf);

                if (i >= 1 && i <= 99) {
                        pos1 = file1.filesize / 100 * i;
                        pos2 = file2.filesize / 100 * i;
                }
                else if (i >= 100) {
                        pos1 = file1.filesize - steps[cmmMovePage];
                        pos2 = file2.filesize - steps[cmmMovePage];
                }
        }

        else if (strpbrk(buf, "ABCDEFXabcdefx")) {
              pos1 = pos2 = strtol(buf, NULL, 16);
        }

        else {
              pos1 = pos2 = strtol(buf, NULL, 10);
        }

        Size prefix = 1;

        const char* ptr = strpbrk(buf, sPrefix);

        if (ptr) {
                ptr = strchr(sPrefix, *ptr);

                prefix = aPrefix[ptr - sPrefix];
        }

        pos1 *= prefix;
        pos2 *= prefix;

        if (cmd & cmgGotoTop) {
                if (rel) {
                        file1.repeatOff = rel > 0 ? pos1 : -pos1;
                        file1.move(file1.repeatOff);
                }

                else {
                        file1.setLast();
                        file1.moveTo(pos1);
                }
        }

        if (cmd & cmgGotoBottom) {
                if (rel) {
                        file2.repeatOff = rel > 0 ? pos2 : -pos2;
                        file2.move(file2.repeatOff);
                }

                else {
                        file2.setLast();
                        file2.moveTo(pos2);
                }
        }
} // end gotoPosition

//--------------------------------------------------------------------
// Search for text or bytes in the files

void searchFiles(Command cmd)
{
        const bool havePrev = !lastSearch.empty();
        int key = 0;

        if (! ((cmd & cmfFindNext || cmd & cmfFindPrev) && havePrev)) {
                positionInWin(cmd, (havePrev ? 36 : 18), " Find ");

                mvwaddstr(winInput, 1,  2, "H Hex");
                mvwaddstr(winInput, 1, 10, "T Text");

                mvwchgat(winInput, 1,  2, 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);
                mvwchgat(winInput, 1, 10, 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);

                if (havePrev) {
                        mvwaddstr(winInput, 1, 19, "N Next");
                        mvwaddstr(winInput, 1, 28, "P Prev");

                        mvwchgat(winInput,  1, 19, 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);
                        mvwchgat(winInput,  1, 28, 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);
                }

                key = upCase(wgetch(winInput));

                bool hex = false;

                if (key == KEY_ESCAPE) {
                        return;
                }
                else if (key == 'H') {
                        hex = true;
                }

                if (! ((key == 'N' || key == 'P') && havePrev)) {
                        positionInWin(cmd, screenWidth, (hex ? " Find Hex Bytes " : " Find Text "));

                        int maxlen = screenWidth - 4 - 1;

                        if (hex) {
                                maxlen -= maxlen % 3;
                        }

                        char buf[maxlen + 1];
                        int searchLen;

                        if (hex) {
                                getString(buf, maxlen, hexSearchHistory, hexDigits, true, true);

                                searchLen = packHex(buf);
                        }
                        else {
                                getString(buf, maxlen, textSearchHistory);

                                searchLen = strlen(buf);
                        }

                        if (! searchLen) {
                                return;
                        }

                        if (cmd & cmgGotoTop) {
                                file1.setLast();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.setLast();
                        }

                        lastSearch.assign(buf, searchLen);

                        lowCase((Byte*)buf, searchLen);

                        lastSearchIgnCase.assign(buf, searchLen);
                }

                if (! singleFile) {
                        file2.updateF();  // kick remnants
                }
        }

        Byte* searchPattern = (Byte*) (ignoreCase ? lastSearchIgnCase.data() : lastSearch.data());

        if (cmd & cmfFindPrev || key == 'P') {
                if (cmd & cmgGotoTop) {
                        file1.busy(true, false, true);

                        file1.moveBack(searchPattern, lastSearch.size());
                        file1.busy();
                }

                if (cmd & cmgGotoBottom) {
                        file2.busy(true, false, true);

                        file2.moveBack(searchPattern, lastSearch.size());
                        file2.busy();
                }
        }

        else {
                if (cmd & cmgGotoTop) {
                        file1.busy(true, false, true);

                        file1.moveForw(searchPattern, lastSearch.size());
                        file1.busy();
                }

                if (cmd & cmgGotoBottom) {
                        file2.busy(true, false, true);

                        file2.moveForw(searchPattern, lastSearch.size());
                        file2.busy();
                }
        }
} // end searchFiles

//--------------------------------------------------------------------
// Handle a command  ##:hand

void handleCmd(Command cmd)
{
        if (cmd & cmgGoto) {
                if (cmd & cmgGotoForw) {
                        if (cmd & cmgGotoTop) {
                                file1.skip();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.skip();
                        }
                }

                else if (cmd & cmgGotoBack) {
                        if (cmd & cmgGotoTop) {
                                file1.skip(true);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.skip(true);
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoLGet) {
                        if (cmd & cmgGotoTop) {
                                file1.getLast();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.getLast();
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoLSet) {
                        if (cmd & cmgGotoTop) {
                                file1.setLast();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.setLast();
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoLOff) {
                        if (cmd & cmgGotoTop) {
                                file1.move(file1.repeatOff);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.move(file2.repeatOff);
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoNOff) {
                        if (cmd & cmgGotoTop) {
                                file1.move(-file1.repeatOff);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.move(-file2.repeatOff);
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoJGet) {
                        if (cmd & cmgGotoTop) {
                                file1.setLast();
                                file1.moveTo(file1.startAddr);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.setLast();
                                file2.moveTo(file2.startAddr);
                        }
                }

                else if ((cmd & cmgGotoMask) == cmgGotoJSet) {
                        if (cmd & cmgGotoTop) {
                                file1.setJump();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.setJump();
                        }
                }

                else {
                        gotoPosition(cmd);
                }
        }

        else if (cmd & cmfFind) {
                if (cmd & cmfNotCharDn) {
                        if (cmd & cmgGotoTop) {
                                file1.busy(true);

                                file1.seekForw();
                                file1.busy();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.busy(true);

                                file2.seekForw();
                                file2.busy();
                        }
                }

                else if (cmd & cmfNotCharUp) {
                        if (cmd & cmgGotoTop) {
                                file1.busy(true);

                                file1.seekBack();
                                file1.busy();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.busy(true);

                                file2.seekBack();
                                file2.busy();
                        }
                }

                else {
                        searchFiles(cmd);
                }
        }

        else if (cmd & cmmMove) {
                int step = steps[cmd & cmmMoveMask];

                if (! (cmd & cmmMoveForward)) {
                        step *= -1;
                }

                if ((cmd & cmmMoveForward) && ! step) {  // special case first
                        if (cmd & cmgGotoTop) {
                                file1.setLast();
                                file1.moveToEnd();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.setLast();
                                file2.moveToEnd();
                        }
                }

                else {
                        if (cmd & cmgGotoTop) {
                                if (step) {
                                        file1.move(step);

                                        if (haveDiff) {
                                                diffs.compute();
                                        }
                                }
                                else {
                                        file1.setLast();
                                        file1.moveTo(0);
                                }
                        }

                        if (cmd & cmgGotoBottom) {
                                if (step) {
                                        file2.move(step);

                                        if (haveDiff) {
                                                diffs.compute();
                                        }
                                }
                                else {
                                        file2.setLast();
                                        file2.moveTo(0);
                                }
                        }
                }
        }

        else if (cmd == cmSyncUp) {
                file1.sync(&file2);
        }

        else if (cmd == cmSyncDn) {
                file2.sync(&file1);
        }

        else if (cmd == cmNextDiff || cmd == cmPrevDiff) {
                if (lockState) {
                        lockState = lockNeither;
                }

                file1.busy(true);
                file2.busy(true);

                diffs.differ(cmd);

                diffs.compute();

                file1.busy();
                file2.busy();
        }

        else if (cmd == cmUseTop) {
                if (lockState == lockBottom) {
                        lockState = lockNeither;
                }
                else {
                        lockState = lockBottom;
                }
        }

        else if (cmd == cmUseBottom) {
                if (lockState == lockTop) {
                        lockState = lockNeither;
                }
                else {
                        lockState = lockTop;
                }
        }

        else if (cmd == cmShowAscii) {
                modeAscii ^= true;

                setViewMode();
                file1.resizeF();
                file1.move(0);
        }

        else if (cmd == cmIgnoreCase) {
                file1.busy(true, true);
                file2.busy(true, true);

                ignoreCase ^= true;
                file1.busy(false, true);
                file2.busy(false, true);
        }

        else if (cmd == cmShowRaster) {
                showRaster ^= true;
        }

        else if (cmd == cmShowHelp) {
                displayHelp();
        }

        else if (cmd == cmEditTop && ! modeAscii) {
                file1.display();  // reset smartscroll
                file1.highEdit(screenWidth);

                file1.edit(singleFile ? NULL : &file2);
        }

        else if (cmd == cmEditBottom) {
                file2.highEdit(screenWidth);

                file2.edit(&file1);
        }

        else if (cmd == cmSmartScroll) {
                file1.busy(true);

                file1.smartScroll();
                file1.busy();
        }

        file1.display();
        file2.display();

        if (stopRead) {
                stopRead = false;
                napms(500);
                flushinp();
        }
} // end handleCmd

//--------------------------------------------------------------------
// Get a command from keyboard  ##:get

Command getCommand()
{
        Command cmd = cmNothing;

        while (cmd == cmNothing) {
                int key = file1.readKeyF();

                switch (upCase(key)) {
                        case KEY_RIGHT:      cmd = cmmMove | cmmMoveByte | cmmMoveForward; break;
                        case KEY_DOWN:       cmd = cmmMove | cmmMoveLine | cmmMoveForward; break;
                        case ' ':            cmd = cmmMove | cmmMovePage | cmmMoveForward; break;
                        case KEY_END:        cmd = cmmMove | cmmMoveAll  | cmmMoveForward; break;
                        case KEY_LEFT:       cmd = cmmMove | cmmMoveByte;                  break;
                        case KEY_UP:         cmd = cmmMove | cmmMoveLine;                  break;
                        case KEY_BACKSPACE:  cmd = cmmMove | cmmMovePage;                  break;
                        case KEY_HOME:       cmd = cmmMove | cmmMoveAll;                   break;

                        case 'F':        cmd = cmfFind;                break;
                        case 'N':        cmd = cmfFind | cmfFindNext;  break;
                        case 'P':        cmd = cmfFind | cmfFindPrev;  break;
                        case KEY_NPAGE:  cmd = cmfFind | cmfNotCharDn; break;
                        case KEY_PPAGE:  cmd = cmfFind | cmfNotCharUp; break;

                        case 'G':  cmd = cmgGoto;               break;
                        case '+':
                        case '*':
                        case '=':  cmd = cmgGoto | cmgGotoForw; break;
                        case '-':  cmd = cmgGoto | cmgGotoBack; break;
                        case '\'':
                        case '<':  cmd = cmgGoto | cmgGotoLGet; break;
                        case 'L':  cmd = cmgGoto | cmgGotoLSet; break;
                        case '.':  cmd = cmgGoto | cmgGotoLOff; break;
                        case ',':  cmd = cmgGoto | cmgGotoNOff; break;
                        case '"':  cmd = cmgGoto | cmgGotoJGet; break;
                        case 'J':  cmd = cmgGoto | cmgGotoJSet; break;

                        case 'E':  cmd = lockState == lockTop ? cmEditBottom : cmEditTop; break;

                        case KEY_RETURN:  cmd = singleFile ? cmSmartScroll : cmNextDiff; break;

                        case '#':
                        case '\\': if (! singleFile) cmd = cmPrevDiff; break;

                        case 'T':  if (! singleFile) cmd = cmUseTop;    break;
                        case 'B':  if (! singleFile) cmd = cmUseBottom; break;

                        case '1':  if (! singleFile) cmd = cmSyncUp; break;
                        case '2':  if (! singleFile) cmd = cmSyncDn; break;

                        case 'A':  if (singleFile) cmd = cmShowAscii; break;

                        case 'I':  cmd = cmIgnoreCase; break;

                        case 'R':  cmd = cmShowRaster; break;

                        case 'H':  cmd = cmShowHelp; break;

                        case 'Z':  ee(); break;

                        case KEY_ESCAPE:
                                if (! singleFile && lockState != lockNeither)
                                        cmd = lockState == lockTop ? cmUseBottom : cmUseTop;
                                break;  // better off w/o Esc

                        case KEY_CTRL_C:
                        case 'Q':  cmd = cmQuit; break;
                }
        }

        if (cmd & (cmmMove | cmfFind | cmgGoto)) {
                if (lockState != lockTop)
                        cmd |= cmgGotoTop;

                if (lockState != lockBottom && ! singleFile)
                        cmd |= cmgGotoBottom;
        }

        return cmd;
} // end getCommand

//====================================================================
// Main Program  ##:main

int main(int argc, char* argv[])
{
        program = strrchr(*argv, '/');

        program = program ? program + 1 : *argv;

        if (argc == 1) {
                fprintf(stderr,
                        "%s\n"
                        "\n"
                        "\t%s file [file2] [addr] [addr2]                     // ncurses\n"
                        "\n"
                        "\t%s file1 file2 -                                   // diff view\n"
                        "\n"
                        "\t%s file1 file2 --                                  // diff return\n"
                        "\n"
                        "\t%s file -  [start [end]] [length{l$}] [width{w$}]  // dump ascii\n"
                        "\n"
                        "\t%s file -- [start [end]] [length{l$}]              // dump binary\n"
                        "\n"
                        "// type 'h' for help\n"
                        "\n",
                        helpVersion + 1, program, program, program, program, program);

                exit(0);
        }

        singleFile = true;

        if (argc > 2) {
                processArgs(argc, argv);
        }

        if (dumpMode) {
                dumpFile(argv[1]);  // exit
        }

        if (diffMode) {
                FPos ret = diffFile(argv[1], argv[2]);

                if (ret == -1) {
                        exit(0);  // equal
                }

                if (diffMode == 2) {
                        exit(1);  // diff
                }

                file1.startAddr = ret;
                file2.startAddr = ret;
        }

        fprintf(stderr, "%s\n\n", helpVersion + 1);

        if (! initialize()) {
                err(11, "Unable to initialize ncurses");
        }

        string err;

        if (! file1.setFile(argv[1])) {
                err = string("Unable to open ") + argv[1] + ": " + strerror(errno);
        }
        else if (! singleFile && ! file2.setFile(argv[2])) {
                err = string("Unable to open ") + argv[2] + ": " + strerror(errno);
        }
        else if (file1.filesize > 281474976710656) {  // 2**40*256 == 0x10**12 == 256TB
                err = string("File is too big: ") + argv[1];
        }
        else if (! singleFile && file2.filesize > 281474976710656) {
                err = string("File is too big: ") + argv[2];
        }

        if (err.size()) {
                exitMsg(12, err.c_str());
        }

        setup();

        if (diffMode) {
                diffs.differ(cmNextDiff);
                diffs.compute();
        }

        file1.display();
        file2.display();

        for (Command cmd; (cmd = getCommand()) != cmQuit; handleCmd(cmd)) {
                if (! (cmd & cmfFind && ! (cmd & (cmfNotCharDn | cmfNotCharUp | cmgGoto)))) {
                        file1.searchOff = file2.searchOff = 0;
                }

                if (! (cmd == cmNextDiff || cmd == cmPrevDiff || cmd == cmShowRaster ||
                        (((cmd & (cmgGoto | cmfFind | cmmMove)) == cmmMove) && steps[cmd & cmmMoveMask]))) {
                                haveDiff = false;
                }

                if (file1.scrollOff) {
                        if (cmd != cmShowRaster) {
                                file1.move(0);

                                memset(sm4rt, 0, numLines * sizeof(FPos));
                        }

                        if (! (cmd == cmSmartScroll || cmd == cmShowRaster)) {
                                file1.scrollOff = 0;
                        }
                }
        }

        shutdown();

        return 0;
}
