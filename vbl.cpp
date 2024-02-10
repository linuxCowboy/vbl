//--------------------------------------------------------------------
//
//   VBinDiff for Linux
//
//   Hex viewer, differ and editor
//
//   Copyright 2021-2024 by linuxCowboy
//
//   vbindiff       by Christopher J. Madsen
//   64GB           by Bradley Grainger
//   dynamic width  by Christophe Bucher
//
//   Version:
//      1.x     classic vbindiff interface, fix 32 byte
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

#include <string>
#include <deque>

#include <ncurses.h>

using namespace std;

#define VBL_VERSION     "3.6.1"

/* set Cursor Color in input window:
   - set it when curs_set(2) has no effect

   - uses OSC (Operating System Command)

   - cmdline override:
        meson setup [--wipe] -Dcpp_args=-DSET_CURSOR_COLOR=0|1 vbl
        meson configure      -Dcpp_args=-DSET_CURSOR_COLOR=0|1 vbl

        meson compile -C vbl */
#ifndef SET_CURSOR_COLOR
#define SET_CURSOR_COLOR        0
#endif

/* show a summary in edit insert/delete after large writes */
#ifndef SHOW_WRITE_SUMMARY
#define SHOW_WRITE_SUMMARY      0
#endif

bool debug = 1;
/* curses debug:
f=/tmp/.vbl
tail -F $f
vbl file 2>$f; cat $f
*/
#define mPI(x)          if (debug) fprintf(stderr, "\r%s: 0x%lX %ld \n",  #x, (long)  x,         (long) x);
#define mPU(x)          if (debug) fprintf(stderr, "\r%s: 0x%lX %ld \n",  #x, (Full)  x,         (Full) x);
/* profiling: ns */
#define mPF(x)          if (debug) fprintf(stderr, "\r%s: %.3f 0x%lX \n", #x, (float) x/1000000, (Full) x);
#define mPS(x)          if (debug) fprintf(stderr, "\r%s: %s \n",         #x,         x);
/* hex dump: pointer, count */
#define mPX(x, c)       if (debug) fprintf(stderr, "\r%s, %d \n\r",       #x, (int)   c);\
                                   for (Word i=0; i < c; ++i) fprintf(stderr, "%X ", x[i]);\
                                   fprintf(stderr, "\n");
/* w/o redirection */
#define mPP(x)          if (debug) sleep(x);
#define mPK             if (debug) file1.readKeyF();

#define mEdit           historyPos = history.size();
#define mScale          count   / (scale ? scale : 1)
#define mScaleInc       count++ / (scale ? scale : 1)

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
        cRaster,
        cAddress,
        cHotkey,
        cHighFile,
        cHighBusy,
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
        pairRedWhite,    // cSearch
        pairBlackCyan,   // cRaster
        pairYellowBlue,  // cAddress
        pairGreenBlue,   // cHotkey
        pairWhiteCyan,   // cHighFile
        pairWhiteRed,    // cHighBusy
        pairBlackYellow  // cHighEdit
};

static const attr_t attribStyle[] = {
                    COLOR_PAIR(colorStyle[ cMainWin  ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cInputWin ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHelpWin  ]),
                    COLOR_PAIR(colorStyle[ cName     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cDiff     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cEdit     ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cInsert   ]),
                    COLOR_PAIR(colorStyle[ cSearch   ]),
                    COLOR_PAIR(colorStyle[ cRaster   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cAddress  ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHotkey   ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHighFile ]),
        A_BOLD    | COLOR_PAIR(colorStyle[ cHighBusy ]),
                    COLOR_PAIR(colorStyle[ cHighEdit ])
};

//====================================================================
// Type definitions

typedef unsigned char   Byte;
typedef unsigned short  Word;
typedef unsigned int    Half;
typedef unsigned long   Full;
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
const Command   cmgGotoLSet    = 0x10;
const Command   cmgGotoLGet    = 0x01;
const Command   cmgGotoLOff    = 0x02;
const Command   cmgGotoNOff    = 0x03;
const Command   cmgGotoMask    = 0x03;

const Command   cmfFind        = 0x40;  // Main cmd
const Command   cmfFindNext    = 0x20;
const Command   cmfFindPrev    = 0x10;
const Command   cmfNotCharDn   = 0x02;
const Command   cmfNotCharUp   = 0x01;

const Command   cmmMove        = 0x20;  // Main cmd
const Command   cmmMoveForward = 0x10;
const Command   cmmMoveByte    = 0x00;  // Move 1 byte
const Command   cmmMoveLine    = 0x01;  // Move 1 line
const Command   cmmMovePage    = 0x02;  // Move 1 page
const Command   cmmMoveAll     = 0x03;  // Move to begin or end
const Command   cmmMoveMask    = 0x03;

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

           staticSize = 1 << 24,  // size global buffers
           warnResize = 1 << 29,  // confirmation threshold

           maxHistory = 20;

const char *hexDigits     = "0123456789ABCDEF",                     // search
           *hexDigitsGoto = "0123456789ABCDEFabcdef%Xx+-kmgtKMGT",  // goto

           thouSep = ',',  // thousands separator (or '\0')

           *colorInsert = "#00BBBB",  // cursor color "normal"
           *colorDelete = "#EE0000";  // cursor color "very visible"

const wchar_t barSyms[] = {L'▏', L'▎', L'▍', L'▌', L'▋', L'▊', L'▉', L'█'};

const char sPrefix[] = "kmgtKMGT";
const Size aPrefix[] = { 1000, 1000000, 1000000000, 1000000000000,
                         1024, 1048576, 1073741824, 1099511627776 };

//--------------------------------------------------------------------
// Help screen text - max 21 lines (minScreenHeight - 3)  ##:x

const char *aHelp[] = {
"  ",
"  Move:  left right up down   home end    space backspace",
"  ",
"  Find   Next Prev       PgDn PgUp == next/prev diff byte",
"  ",
"  Goto [+-]{dec hex 0x x$}[%|kmgtKMGT]   +4% + * =  -1% -",
"   last addr: get ' <  set l  last offset .  neg offset ,",
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
        7,19, 7,21,  7,28,  7,43,  7,57,
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

alignas(0x100000)
Byte bufFile1[staticSize],
     bufFile2[staticSize];

Byte *buffer = bufFile1;

char bufTimer[64];

bool singleFile,
     showRaster,
     sizeTera,
     modeAscii,
     ignoreCase,
     stopRead;

int haveDiff;

LockState lockState;

string lastSearch,
       lastSearchIgnCase;

StrDeq hexSearchHistory,
       textSearchHistory,
       positionHistory;

BytDeq editBytes,
       editColor;

// Set dynamically for 16/24/32 byte width
int screenWidth,   // Number of columns in curses
    linesTotal,    // Number of lines in curses
    numLines,      // Number of lines of each file to display
    bufSize,       // Number of bytes of each file to display
    lineWidth,     // Number of bytes displayed per line
    lineWidthAsc,  // Number of bytes displayed per line ascii
    inWidth,       // Number of digits in input window
    leftMar,       // Starting column of hex display
    leftMar2,      // Starting column of ASCII display
    searchIndent,  // Lines of search result indentation
    steps[4];      // Number of bytes to move for each step

//====================================================================
// Global Functions

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

        return ret;
}

FPos SeekFile(File file, FPos position, int whence=SEEK_SET)
{
        return lseek(file, position, whence);
}

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
        for (Size i=0; i < len; ++i) {
                if (buf[i] <= 'Z' && buf[i] >= 'A') {
                        buf[i] |= 0x20;
                }
        }
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
        ConWindow()                                             {}
       ~ConWindow()                                             { delwin(winW); winW = NULL; }

        void            initW(short x, short y, short width, short height, Style style);
        void            updateW()                               { touchwin(winW); wrefresh(winW); }
        int             readKeyW()                              { return wgetch(winW); }

        void            put(short x, short y, const char* s)    { mvwaddstr(winW, y, x, s); }
        void            setAttribs(Style color)                 { wattrset(winW, attribStyle[color]); }
        void            putAttribs(short x, short y, Style color, short count);

        void            setCursor(short x, short y)             { wmove(winW, y, x); }

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

//--------------------------------------------------------------------
// Change the attributes of characters in the window

void ConWindow::putAttribs(short x, short y, Style color, short count)
{
        mvwchgat(winW, y, x, count, attribStyle[color], colorStyle[color], NULL);
}

//====================================================================
// Class FileDisplay  ##:file

class Difference;

class FileDisplay
{
    friend class Difference;

        ConWindow               cwinF;

        const Difference       *diffsF;

        char                   *fileName;
        File                    fd;
        bool                    editable;

        Byte                   *dataF;
        int                     dataSize;
        FPos                    offset;
        FPos                    prevOffset;
        FPos                    diffOffset;
        FPos                    lastOffset;

        FPos                   *addr;
        int                     se4rch;

    public:
        FPos                    searchOff;
        FPos                    scrollOff;
        FPos                    repeatOff;
        Size                    filesize;
        Size                    laptime;
        bool                    two;

    public:
                FileDisplay()                           {}
               ~FileDisplay();

        bool    setFile(char* FileName);
        void    initF(int y, const Difference* Diff);
        void    resizeF();
        void    updateF()                               { cwinF.updateW(); }
        int     readKeyF()                              { return cwinF.readKeyW(); }

        void    display();
        void    busy(bool on, bool ic);
        void    highEdit(short count);

        void    edit(const FileDisplay* other);
        void    editOut(short outOffset);
        bool    WriteTail(FPos start);
        bool    assure();
        void    progress1();
        void    progress(wchar_t* bar, int count, int delay, int stint);
        Size    finish(int init);

        void    setLast()                               { lastOffset = offset; }
        void    getLast()                               { FPos tmp = offset; moveTo(lastOffset); lastOffset = tmp; }
        void    skip(bool upwards);
        void    sync(const FileDisplay* other);

        void    move(FPos step)                         { moveTo(offset + step); }
        void    moveTo(FPos newOffset);
        void    moveToEnd()                             { moveTo(filesize - steps[cmmMovePage]); }
        void    moveForw(const Byte* searchFor, Size searchLen);
        void    moveBack(const Byte* searchFor, Size searchLen);

        void    seekNotChar(bool upwards);
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
                                        file1D(File1), file2D(File2)  {}
               ~Difference()                                            { delete [] dataD; }
        void    resizeD();
        int     compute(Command cmd);
        void    speedup(int way);
}; // end Difference

//====================================================================
// Object instantiation

FileDisplay     file1, file2;

Difference      diffs(&file1, &file2);

//====================================================================
// Class Difference member functions

void Difference::resizeD()
{
        dataD = new Byte[bufSize];
}

//--------------------------------------------------------------------
// Compute differences  ##:u

int Difference::compute(Command cmd)
{
        haveDiff = 0;
        memset(dataD, 0, bufSize);

        if (! file1D->dataSize) {
                file1.moveToEnd();
        }

        if (! file2D->dataSize) {
                file2.moveToEnd();
        }

        const Byte *buf1 = file1D->dataF,
                   *buf2 = file2D->dataF;

        int size = min(file1D->dataSize, file2D->dataSize);

        int diff = 0;
        for (; diff < size; ++diff) {
                if (*buf1++ != *buf2++) {
                        dataD[diff] = true;
                        haveDiff++;
                }
        }

        size = max(file1D->dataSize, file2D->dataSize);

        for (; diff < size; ++diff) {
                dataD[diff] = true;
                haveDiff++;
        }

        if (cmd == cmPrevDiff && (! file1D->offset || ! file2D->offset)) {
                return 1;
        }

        if (cmd == cmNextDiff && (file1D->dataSize < bufSize || file2D->dataSize < bufSize)) {
                haveDiff = -1;  // move anyway
        }

        return haveDiff;
} // end Difference::compute

//--------------------------------------------------------------------
// Speedup differ - diff in next/prev staticSize bytes

void Difference::speedup(int way)
{
        if (way > 0) {
                SeekFile(file1D->fd, file1D->offset);
                SeekFile(file2D->fd, file2D->offset);

                while (file1D->offset + staticSize < file1.filesize &&
                                file2D->offset + staticSize < file2.filesize && ! stopRead) {
                        ReadFile(file1D->fd, bufFile1, staticSize);
                        ReadFile(file2D->fd, bufFile2, staticSize);

                        if (memcmp(bufFile1, bufFile2, staticSize)) {
                                break;
                        }

                        file1D->offset += staticSize;
                        file2D->offset += staticSize;
                }
        }
        else {  // downwards
                while (file1D->offset - staticSize > 0 &&
                                file2D->offset - staticSize > 0 && ! stopRead) {
                        SeekFile(file1D->fd, file1D->offset - staticSize);
                        SeekFile(file2D->fd, file2D->offset - staticSize);

                        ReadFile(file1D->fd, bufFile1, staticSize);
                        ReadFile(file2D->fd, bufFile2, staticSize);

                        if (memcmp(bufFile1, bufFile2, staticSize)) {
                                break;
                        }

                        file1D->offset -= staticSize;
                        file2D->offset -= staticSize;
                }
        }
} // end Difference::speedup

//====================================================================
// Class FileDisplay member functions

FileDisplay::~FileDisplay()
{
        if (fd) {
                close(fd);
        }

        delete [] dataF;

        free(addr);
}

//--------------------------------------------------------------------
// Open a file for display

bool FileDisplay::setFile(char* FileName)
{
        fileName = FileName;

        File probe = OpenFile(fileName, true);

        if (probe > 0) {
                editable = true;
                close(probe);
        }

        if ((fd = OpenFile(fileName)) < 0) {
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

        dataF = new Byte[bufSize];

        moveTo(offset);
}

//--------------------------------------------------------------------
// Set member variables

void FileDisplay::initF(int y, const Difference* Diff)
{
        diffsF = Diff;
        two = y ? true : false;

        cwinF.initW(0, y, screenWidth, numLines + 1, cMainWin);

        resizeF();

        addr = (FPos*) calloc(numLines, sizeof(FPos));
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
              size_fname = strlen(fileName);

        if (size_fname <= size_name) {
                memcpy(bufStat, fileName, size_fname);
        }
        else {
                first = size_name / 4;

                memcpy(bufStat, fileName, first);
                memcpy(bufStat + first, " ... ", 5);

                last = size_name - first - 5;

                memcpy(bufStat + first + 5, fileName + size_fname - last, last);
        }

        memcpy(bufStat + size_name, buf, strlen(buf));

        cwinF.put(0, 0, bufStat);
        cwinF.putAttribs(0, 0, cName, strlen(bufStat));

        if (lockState == lockBottom && ! two) {
                cwinF.putAttribs(0, 0, cHighFile, size_name);
        }
        else if (lockState == lockTop && two) {
                cwinF.putAttribs(0, 0, cHighFile, size_name);
        }

        if (diffOffset < 0) {
                char *pc = (char*) memchr(buf, '-', strlen(buf));

                cwinF.putAttribs(size_name + (pc - buf), 0, cSearch, 1);
        }

        char bufHex[screenWidth + 1] = { 0 },
             bufAsc[  lineWidth + 1] = { 0 };

        for (row=0; row < numLines; ++row) {
                memset(bufHex, ' ', screenWidth);
                memset(bufAsc, ' ',   lineWidth);

                if (*(addr + row)) {
                        lineOffset += (lineWidth * (*(addr + row)));
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
                cwinF.putAttribs(col, row + 1, cAddress, (sizeTera ? 12 : 9) - col);

                if (showRaster) {
                        if (sizeTera) {
                                cwinF.putAttribs(0, row + 1, cRaster, 1);
                        }
                        cwinF.putAttribs(sizeTera ? 4 : 1, row + 1, cRaster, 1);
                        cwinF.putAttribs(sizeTera ? 8 : 5, row + 1, cRaster, 1);
                }

                if (! modeAscii && showRaster && bufHex[leftMar] != ' ') {
                        for (col=0; col <= lineWidth - 8; col += 8) {
                                cwinF.putAttribs(leftMar  + col * 3 - 1, row + 1, cRaster, 1);
                                cwinF.putAttribs(leftMar2 + col        , row + 1, cRaster, 1);
                        }
                }

                if (haveDiff) {
                        for (col=0; col < lineWidth; ++col) {
                                if (diffsF->dataD[row * lineWidth + col]) {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cDiff, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cDiff, 1);
                                }
                        }
                }

                if (se4rch && row >= (searchOff >= searchIndent ? searchIndent / lineWidth : 0)) {
                        for (col=0; se4rch && col < lineWidth; --se4rch, ++col) {
                                if (modeAscii) {
                                        cwinF.putAttribs(leftMar  + col    , row + 1, cSearch, 1);
                                }
                                else {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cSearch, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cSearch, 1);
                                }
                        }
                }

                if (*(addr + row)) {
                        for (col=0; col < lineWidth; ++col) {
                                if (modeAscii) {
                                        cwinF.putAttribs(leftMar  + col    , row + 1, cDiff, 1);
                                }
                                else {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cDiff, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cDiff, 1);
                                }
                        }
                }

                lineOffset += lineWidth;
        }

        if (scrollOff) {
                moveTo(offset);  // reload buffer

                memset(addr, 0, numLines * sizeof(FPos));
        }

        updateF();
} // end FileDisplay::display

//--------------------------------------------------------------------
// Busy status

void FileDisplay::busy(bool on=false, bool ic=false)
{
        if (on) {
                cwinF.putAttribs(screenWidth - (ic ? 4 : 2),  0, cHighBusy, ic ? 1 : 2);
                updateF();
        }
        else {
                napms(150);
                cwinF.putAttribs(screenWidth - (ic ? 4 : 2),  0, cName, ic ? 1 : 2);

                if (! singleFile && ! two) {
                        updateF();
                }
        }
}

//--------------------------------------------------------------------
// Highlight statusbar

void FileDisplay::highEdit(short count)
{
        cwinF.putAttribs(0, 0, cHighEdit, count);
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

                pbufHex += sprintf(pbufHex, "%0*lX  ", sizeTera ? 12 : 9, lineOffset);

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
                                cwinF.putAttribs(col[i], row + 1, cRaster, 1);
                        }
                }

                if (showRaster && bufHex[leftMar] != ' ') {
                        for (int col=0; col <= lineWidth - 8; col += 8) {
                                cwinF.putAttribs(leftMar  + col * 3 - 1, row + 1, cRaster, 1);
                                cwinF.putAttribs(leftMar2 + col        , row + 1, cRaster, 1);
                        }
                }

                for (int c, col=0; col < lineLength; ++col) {
                        if ((c = editColor[outOffset + row * lineWidth + col])) {
                                cwinF.putAttribs(leftMar  + col * 3, row + 1, (Style) c, 2);
                                cwinF.putAttribs(leftMar2 + col    , row + 1, (Style) c, 1);
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

void FileDisplay::progress(wchar_t* bar, int count, int delay, int stint=1)
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
// Progress bar end (or debug profiling)

Size FileDisplay::finish(int init=0)
{
        timespec ts;

        clock_gettime(CLOCK_TAI, &ts);

        if (init) {
                laptime = ts.tv_sec * 1000000000 + ts.tv_nsec;

                return 0;
        }

        return (ts.tv_sec * 1000000000 + ts.tv_nsec - laptime);
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
            loops = remain / cargo,
            scale = 0,
            delay = 4,
            count = 0,
            stage = 0,
            tally = 0,
            final = 0;

        Size round = 0,
             chunk = 0;

        if (! loops) {
                progress1();
        }

        else if (loops > width) {
                scale = loops / width + (loops % width ? 1 : 0);
                width = loops / scale + (loops % scale ? 1 : 0);
        }

        else if (loops > level) {
                width = loops;
        }

        else {
                cargo = remain / level >> 12;
                cargo *= 4096;  // page align

                width = remain / cargo;
        }

        stage = width - 8;

        width = width / 8 + (width % 8 ? 1 : 0);

        wchar_t bar[width + 1];
        memset(bar, 0, sizeof(bar));

        if (loops) {
                positionInWin(two ? cmgGotoBottom : cmgGotoTop, 2+ width +2, "");
        }

#if SHOW_WRITE_SUMMARY
        int term = time(NULL);
#endif
        if (remain >= cargo) {
                if (insert) {
                        srcOff = filesize;  // downwards
                        dstOff = filesize + start - dataSize;
                }

                while (remain >= cargo) {
                        if (mScale > stage) {  // use only the last ones for timekeeping
                                finish(1);
                        }

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

                        if (mScale > stage) {
                                round += finish() + 1;  // assure non-zero
                        }

                        if (scale && count % scale) {
                                count++;
                                continue;
                        }

                        if (round) {
                                if (! scale || tally++) {  // discard single round
                                        chunk += round;
                                        final++;
                                }

                                round = 0;
                        }

                        progress(bar, mScaleInc, delay);
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
        if (loops) {
                for (;;) {
                        if (scale && count % scale) {
                                ++count;
                        }

                        else if (mScale % 8) {  // neat finish
                                progress(bar, mScaleInc, chunk / final / 1000000 + delay);
                        }

                        else {
                                break;
                        }
                }
                napms(600);
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
                fd = OpenFile(fileName, true);

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

                fd = OpenFile(fileName);

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

void FileDisplay::moveForw(const Byte* searchFor, Size searchLen)
{
        FPos newPos = searchOff > 0 ? searchOff + 1 : (searchOff < 0 ? 1 : offset);
        Full leader = 0;
        Size bias   = 0;

        while (! *(searchFor + bias) && bias < searchLen) {
                ++bias;
        }

        if (bias == searchLen) {
                bias = 0;
        }
        else {
                for (Size i=0; i < 8; ++i) {
                        leader = leader << 8 | *(searchFor + bias);
                }
        }

        for (;;) {
                SeekFile(fd, newPos);
                Size bytesRead = ReadFile(fd, buffer, staticSize);

                if (bytesRead < searchLen || stopRead) {
                        break;
                }

                if (ignoreCase) {
                        lowCase(buffer, bytesRead);
                }

                for (Size i=0; i <= bytesRead - searchLen; ++i) {
                        Full turbo = *(Full*) (buffer + i + bias);

                        if (! turbo) {
                                if (leader) {
                                        goto incr;
                                }
                                else {
                                        goto skip;
                                }
                        }

                        turbo ^= leader;

                        if      (! (turbo & 0x00000000000000FF)) { i += 0; }
                        else if (! (turbo & 0x000000000000FF00)) { i += 1; }
                        else if (! (turbo & 0x0000000000FF0000)) { i += 2; }
                        else if (! (turbo & 0x00000000FF000000)) { i += 3; }
                        else if (! (turbo & 0x000000FF00000000)) { i += 4; }
                        else if (! (turbo & 0x0000FF0000000000)) { i += 5; }
                        else if (! (turbo & 0x00FF000000000000)) { i += 6; }
                        else if (! (turbo & 0xFF00000000000000)) { i += 7; }
                        else {
incr:                           i += 7;
cont:                           continue;
                        }

skip:                   if (searchFor[searchLen - 1] == buffer[i + searchLen - 1]) {
                                Size j = 0;
                                for (; j + 7 < searchLen; j+=8) {
                                        if (*(Full*) (searchFor  + j) !=
                                            *(Full*) (buffer + i + j)) {
                                                goto cont;
                                        }
                                }

                                if (searchLen != j) {  // shl mask 63
                                        if ((*(Full*) (searchFor  + j) ^
                                             *(Full*) (buffer + i + j) )
                                             << 8 * (8 - searchLen + j)) {
                                                goto cont;
                                        }
                                }

                                if (i > bytesRead - searchLen) {  // limit turbo
                                        goto cont;
                                }

                                newPos    = newPos + i;
                                searchOff = newPos ? newPos : -1;  // tri-state
                                se4rch    = searchLen;

                                moveTo(newPos - (searchOff >= searchIndent ? searchIndent : 0));
                                return;
                        }
                }

                newPos += staticSize - searchLen + 1;
        }

        moveTo(stopRead ? newPos : filesize);

        searchOff = 0;
} // end FileDisplay::moveForw

//--------------------------------------------------------------------
// Change the file position by searching backwards

void FileDisplay::moveBack(const Byte* searchFor, Size searchLen)
{
        FPos newPos = searchOff > 0 ? searchOff : offset;
        Full leader = 0;
        Size bias   = 0;

        while (! *(searchFor + bias) && bias < searchLen) {
                ++bias;
        }

        if (bias == searchLen) {
                bias = 0;
        }
        else {
                for (Size i=0; i < 8; ++i) {
                        leader = leader << 8 | *(searchFor + bias);
                }
        }

        if (newPos + searchLen - 1 > filesize) {
                newPos = filesize - searchLen + 1;
        }

        for (;;) {
                newPos -= staticSize - searchLen + 1;

                SeekFile(fd, newPos > 0 ? newPos : 0);
                Size bytesRead = ReadFile(fd, buffer, staticSize);

                if (ignoreCase) {
                        lowCase(buffer, bytesRead);
                }

                for (Size i = staticSize + (newPos < 0 ? newPos : 0) - searchLen; i >= 0; --i) {
                        Full turbo = *(Full*) (buffer + i + bias - 7);

                        if (! turbo) {
                                if (leader) {
                                        goto decr;
                                }
                                else {
                                        goto skip;
                                }
                        }

                        turbo ^= leader;

                        if      (! (turbo & 0xFF00000000000000)) { i -= 0; }
                        else if (! (turbo & 0x00FF000000000000)) { i -= 1; }
                        else if (! (turbo & 0x0000FF0000000000)) { i -= 2; }
                        else if (! (turbo & 0x000000FF00000000)) { i -= 3; }
                        else if (! (turbo & 0x00000000FF000000)) { i -= 4; }
                        else if (! (turbo & 0x0000000000FF0000)) { i -= 5; }
                        else if (! (turbo & 0x000000000000FF00)) { i -= 6; }
                        else if (! (turbo & 0x00000000000000FF)) { i -= 7; }
                        else {
decr:                           i -= 7;
cont:                           continue;
                        }

skip:                   if (searchFor[searchLen - 1] == buffer[i + searchLen - 1]) {
                                Size j = 0;
                                for (; j + 7 < searchLen; j+=8) {
                                        if (*(Full*) (searchFor  + j) !=
                                            *(Full*) (buffer + i + j)) {
                                                goto cont;
                                        }
                                }

                                if (searchLen != j) {
                                        if ((*(Full*) (searchFor  + j) ^
                                             *(Full*) (buffer + i + j) )
                                             << 8 * (8 - searchLen + j)) {
                                                goto cont;
                                        }
                                }

                                if (i < 0) {
                                        goto cont;
                                }

                                newPos    = (newPos > 0 ? newPos : 0) + i;
                                searchOff = newPos ? newPos : -1;
                                se4rch    = searchLen;

                                moveTo(newPos - (searchOff >= searchIndent ? searchIndent : 0));
                                return;
                        }
                }

                if (newPos <= 0 || stopRead) {
                        break;
                }
        }

        moveTo(stopRead ? newPos : 0);

        searchOff = 0;
} // end FileDisplay::moveBack

//--------------------------------------------------------------------
// Seek to next byte not equal to current head

void FileDisplay::seekNotChar(bool upwards=false)
{
        const int blockSize = 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize];

        Byte searchFor = *dataF;
        if (modeAscii && ! isprint(searchFor)) { searchFor = ' '; }

        FPos newPos = upwards ? offset - blockSize : offset + 1;
        int bytesRead;
        int diff = 0, here = -1;

        for (;;) {
                if (newPos < 0) { diff = newPos; newPos = 0; }
                SeekFile(fd, newPos);
                if (((bytesRead = ReadFile(fd, searchBuf, blockSize)) <= 0) || stopRead) break;

                if (modeAscii) {
                        for (int i=0; i < bytesRead; ++i) {
                                if (! ((unsigned) searchBuf[i] - ' ' < '_')) {
                                        searchBuf[i] = ' ';
                                }
                        }
                }

                if (upwards) {
                        for (int i = blockSize - 1 + diff; i >= 0; --i) {
                                if (searchBuf[i] != searchFor) {
                                        here = i; goto done;
                                }
                        }
                        if (! newPos) break;
                        newPos -= blockSize;
                }
                else {
                        for (int i=0; i < bytesRead; ++i) {
                                if (searchBuf[i] != searchFor) {
                                        here = i; goto done;
                                }
                        }
                        newPos += blockSize;
                }
        }
done:
        delete [] searchBuf;
        if      (here >= 0) { moveTo(newPos + here); se4rch = 1; }
        else if (stopRead)  { moveTo(newPos); }
        else if (upwards)   { moveTo(0); }
        else                { moveToEnd(); }
} // end FileDisplay::seekNotChar

//--------------------------------------------------------------------
// Scroll forward with skipping same content lines

void FileDisplay::smartScroll()
{
        FPos newPos = scrollOff ? scrollOff : (offset & ~0xF) + steps[cmmMovePage];

        if (filesize - newPos < bufSize) {
                scrollOff = 0;
                moveTo(newPos);

                if (! dataSize) {
                        moveToEnd();
                }
                return;
        }
        offset = newPos;

        const int blockSize = 1000 * bufSize;
        Byte* scrollBuf = new Byte[blockSize];

        Byte buf[lineWidth];
        FPos repeat = 0;

        SeekFile(fd, newPos);
        int bytesRead = ReadFile(fd, scrollBuf, blockSize);

        memcpy(dataF, scrollBuf, lineWidth);
        bytesRead -= lineWidth;

        if (modeAscii) {
                for (int i=0; i < lineWidth; ++i) {
                        if (! isprint(dataF[i])) {
                                dataF[i] = ' ';
                        }
                }
        }

        int i = 1, j = 1;
        for (; bytesRead > 0;) {
                if (bytesRead >= lineWidth) {
                        memcpy(buf, scrollBuf + j * lineWidth, lineWidth);

                        if (modeAscii) {
                                for (int k=0; k < lineWidth; ++k) {
                                        if (! isprint(buf[k])) {
                                                buf[k] = ' ';
                                        }
                                }
                        }

                        if (memcmp(dataF + (i - 1) * lineWidth, buf, lineWidth)) {
                                memcpy(dataF + i * lineWidth, buf, lineWidth);

                                *(addr + i) = repeat;
                                repeat = 0;
                                ++i;

                        }
                        else {
                                ++repeat;
                        }

                        if (i == numLines) {
                                --i;
                                break;
                        }

                        bytesRead -= lineWidth;
                        ++j;
                }

                if (bytesRead < lineWidth) {
                        newPos += j * lineWidth;
                        j = 0;

                        SeekFile(fd, newPos);
                        bytesRead = ReadFile(fd, scrollBuf, blockSize);

                        if (bytesRead > 0) {
                                if (bytesRead < lineWidth || stopRead) {
                                        *(addr + i) = repeat;

                                        memcpy(dataF + i * lineWidth, scrollBuf, min(bytesRead, lineWidth));
                                        break;
                                }

                        }

                        else {
                                if (repeat) {
                                        *(addr + i) = --repeat;

                                        memcpy(dataF + i * lineWidth, dataF + (i - 1) * lineWidth, lineWidth);
                                        ++i;
                                }
                        }
                }
        }

        /* Gosh! All 4 exit points land here. */
        scrollOff = newPos + j * lineWidth;

        dataSize = i * lineWidth + min(bytesRead, lineWidth);

        delete [] scrollBuf;
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

        mvwaddstr(winHelp, 0,              (helpWidth - 6)                   / 2, " Help ");
        mvwaddstr(winHelp, helpHeight - 1, (helpWidth - strlen(helpVersion)) / 2, helpVersion);

        for (int i=0; i < helpHeight - 2; ++i) {  // exclude border
                mvwaddstr(winHelp, i + 1, 1, aHelp[i]);
        }

        for (int i=0; aBold[i]; i += 2) {
                mvwchgat(winHelp, aBold[i], aBold[i + 1], 1, attribStyle[cHotkey], colorStyle[cHotkey], NULL);
        }

        if (! singleFile) {
                diffs.resizeD();
        }

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
                                case KEY_UP:     if (naps < 50) ++naps; break;
                                case KEY_DOWN:   if (naps >  0) --naps; break;

                                case KEY_LEFT:   if (blocks > 3) --blocks; file1.updateF(); break;
                                case KEY_RIGHT:  if (blocks < screenWidth - 4) ++blocks;    break;

                                case KEY_ESCAPE:  file1.updateF(); return;
                                default:          --i;
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
              pos1 = pos2 = strtoull(buf, NULL, 16);
        }

        else {
              pos1 = pos2 = strtoull(buf, NULL, 10);
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
                        file1.busy(true);

                        file1.moveBack(searchPattern, lastSearch.size());
                        file1.busy();
                }

                if (cmd & cmgGotoBottom) {
                        file2.busy(true);

                        file2.moveBack(searchPattern, lastSearch.size());
                        file2.busy();
                }
        }
        else {
                if (cmd & cmgGotoTop) {
                        file1.busy(true);

                        file1.moveForw(searchPattern, lastSearch.size());
                        file1.busy();
                }

                if (cmd & cmgGotoBottom) {
                        file2.busy(true);

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

                else if (cmd & cmgGotoLSet) {
                        if (cmd & cmgGotoTop) {
                                file1.setLast();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.setLast();
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

                else {
                        gotoPosition(cmd);
                }
        }

        else if (cmd & cmfFind) {
                if (cmd & cmfNotCharDn) {
                        if (cmd & cmgGotoTop) {
                                file1.busy(true);

                                file1.seekNotChar();
                                file1.busy();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.busy(true);

                                file2.seekNotChar();
                                file2.busy();
                        }
                }

                else if (cmd & cmfNotCharUp) {
                        if (cmd & cmgGotoTop) {
                                file1.busy(true);

                                file1.seekNotChar(true);
                                file1.busy();
                        }

                        if (cmd & cmgGotoBottom) {
                                file2.busy(true);

                                file2.seekNotChar(true);
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
                                }
                                else {
                                        file1.setLast();
                                        file1.moveTo(0);
                                }
                        }

                        if (cmd & cmgGotoBottom) {
                                if (step) {
                                        file2.move(step);
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
                int size = cmd == cmNextDiff ? bufSize : -bufSize;

                if (lockState) {
                        lockState = lockNeither;
                }

                file1.busy(true);
                file2.busy(true);

                if (haveDiff) {
                        file1.move(size);
                        file2.move(size);
                }

                for (int go=1; ! diffs.compute(cmd) && ! stopRead; go=0) {
                        if (go) {
                                diffs.speedup(size);

                                file1.move(0);
                                file2.move(0);
                        }

                        else {
                                file1.move(size);
                                file2.move(size);
                        }
                }

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
        char* prog = strrchr(*argv, '/');

        prog = prog ? prog + 1 : *argv;

        printf("%s\n\n", helpVersion + 1);

        if (argc < 2 || argc > 3) {
                printf("\t%s file1 [file2]\n"
                        "\n"
                        "// type 'h' for help\n"
                        "\n",
                        prog);

                exit(0);
        }

        singleFile = (argc == 2);

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

        file1.display();
        file2.display();

        for (Command cmd; (cmd = getCommand()) != cmQuit; handleCmd(cmd)) {
                if (! (cmd & cmfFind && ! (cmd & (cmfNotCharDn | cmfNotCharUp | cmgGoto)))) {
                        file1.searchOff = file2.searchOff = 0;
                }

                if (! (cmd == cmNextDiff || cmd == cmPrevDiff)) {
                        haveDiff = 0;
                }

                if (cmd != cmSmartScroll) {
                        file1.scrollOff = 0;
                }
        }

        shutdown();

        return 0;
}
