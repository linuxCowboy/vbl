//--------------------------------------------------------------------
//
//   VBinDiff for Linux
//
//   Copyright 1995-2017 by Christopher J. Madsen
//   Copyright 2021-2023 by linuxCowboy
//
//   64GB by Bradley Grainger
//   dynamic width by Christophe Bucher
//
//   Visual display of differences in binary files
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
//   You should have received a copy of the GNU General Public License
//   along with this program.  If not, see <https://www.gnu.org/licenses/>.
//--------------------------------------------------------------------

#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <deque>

#include <ncurses.h>

using namespace std;

#define VBL_VERSION     "2.16"

/* set Cursor Color in input window: but _REMAINS_ after Exit!
   - set it when curs_set(2) has no effect

   - cmdline override:
        meson setup [--wipe] -Dcpp_args=-DSET_CURSOR_COLOR=0|1 vbl
        meson configure      -Dcpp_args=-DSET_CURSOR_COLOR=0|1 vbl

        meson compile -C vbl */
#ifndef SET_CURSOR_COLOR
#define SET_CURSOR_COLOR        0
#endif

/* curses debug: vbl file 2>/tmp/.vbl; cat /tmp/.vbl */
#define mPi(x)          fprintf(stderr, "%s: %ld 0x%lX \n", #x, (long unsigned int) x, (long unsigned int) x);
#define mPs(x)          fprintf(stderr, "%s: %s \n", #x, x);

#define KEY_CTRL_C      0x03
#define KEY_TAB         0x09
#define KEY_RETURN      0x0D
#define KEY_ESCAPE      0x1B
#define KEY_DELETE      0x7F

enum Style {
  cBackground,
  cWindow,
  cWindowBold,
  cCurrentMode,
  cFileName,
  cFileWin,
  cFileDiff,
  cFileEdit,
  cFileSearch,
  cFileMark,
  cFileAddr,
  cHotKey,
  cHighFile,
  cHighBusy,
  cHighEdit
};

enum ColorPair {
  pairWhiteBlue = 1,
  pairWhiteBlack,
  pairRedWhite,
  pairYellowBlue,
  pairGreenBlue,
  pairBlackCyan,
  pairGreenBlack,
  pairWhiteCyan,
  pairWhiteRed,
  pairBlackYellow
};

static const ColorPair colorStyle[] = {
  pairWhiteBlue,   // cBackground
  pairWhiteBlue,   // cWindow
  pairWhiteBlue,   // cWindowBold
  pairWhiteBlack,  // cCurrentMode
  pairWhiteBlack,  // cFileName
  pairWhiteBlue,   // cFileWin
  pairGreenBlack,  // cFileDiff
  pairYellowBlue,  // cFileEdit
  pairRedWhite,    // cFileSearch
  pairBlackCyan,   // cFileMark
  pairYellowBlue,  // cFileAddr
  pairGreenBlue,   // cHotKey
  pairWhiteCyan,   // cHighFile
  pairWhiteRed,    // cHighBusy
  pairBlackYellow  // cHighEdit
};

static const attr_t attribStyle[] = {
              COLOR_PAIR(colorStyle[ cBackground ]),
              COLOR_PAIR(colorStyle[ cWindow     ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cWindowBold ]),
  A_REVERSE | COLOR_PAIR(colorStyle[ cCurrentMode]),
  A_REVERSE | COLOR_PAIR(colorStyle[ cFileName   ]),
              COLOR_PAIR(colorStyle[ cFileWin    ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileDiff   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileEdit   ]),
              COLOR_PAIR(colorStyle[ cFileSearch ]),
              COLOR_PAIR(colorStyle[ cFileMark   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileAddr   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cHotKey     ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cHighFile   ]),
              COLOR_PAIR(colorStyle[ cHighBusy   ]),
              COLOR_PAIR(colorStyle[ cHighEdit   ])
};

//====================================================================
// Type definitions

typedef unsigned char   Byte;
typedef Byte            Command;

typedef int             File;
typedef off_t           FPos;  // long int
typedef ssize_t         Size;  // long int
  // typedef long unsigned int size_t;

typedef string          String;
typedef deque<String>   StrDeq;

enum LockState { lockNeither, lockTop, lockBottom };

//====================================================================
// Constants   ##:cmd

const Command  cmgGoto        = 0x80;  // Main cmd
const Command  cmgGotoTop     = 0x08;  // Flag
const Command  cmgGotoBottom  = 0x04;  // Flag
const Command  cmgGotoNOff    = 0x40;
const Command  cmgGotoLOff    = 0x20;
const Command  cmgGotoLast    = 0x10;
const Command  cmgGotoForw    = 0x02;
const Command  cmgGotoBack    = 0x01;

const Command  cmfFind        = 0x40;  // Main cmd
const Command  cmfFindNext    = 0x20;
const Command  cmfFindPrev    = 0x10;
const Command  cmfNotCharDn   = 0x02;
const Command  cmfNotCharUp   = 0x01;

const Command  cmmMove        = 0x20;  // Main cmd
const Command  cmmMoveForward = 0x10;
const Command  cmmMoveByte    = 0x00;  // Move 1 byte
const Command  cmmMoveLine    = 0x01;  // Move 1 line
const Command  cmmMovePage    = 0x02;  // Move 1 page
const Command  cmmMoveAll     = 0x03;  // Move to beginning or end
const Command  cmmMoveSize    = 0x03;  // Mask

const Command  cmNothing      = 0;
const Command  cmUseTop       = 1;
const Command  cmUseBottom    = 2;
const Command  cmNextDiff     = 3;
const Command  cmPrevDiff     = 4;
const Command  cmEditTop      = 5;
const Command  cmEditBottom   = 6;
const Command  cmSyncUp       = 7;
const Command  cmSyncDn       = 8;
const Command  cmShowAscii    = 9;
const Command  cmIgnoreCase   = 10;
const Command  cmShowRaster   = 11;
const Command  cmShowHelp     = 12;
const Command  cmSmartScroll  = 13;
const Command  cmQuit         = 14;

//--------------------------------------------------------------------

const int minScreenHeight = 24;  // Enforced minimum height
const int minScreenWidth  = 79;  // Enforced minimum width

const int skipForw = 4;  // Percent to skip forward
const int skipBack = 1;  // Percent to skip backward

const int maxPath = 2000;
const int sizeReadBuf = 0x1000000;  // speedup compute()

const int maxHistory = 50;

const char hexDigits[] = "0123456789ABCDEF%X+-";  // with Goto %, hex and rel

const char thouSep = '.';  // thousands separator (or '\0')

const File InvalidFile = -1;

const char colorInsert[] = "#00BBBB";  // cursor color "normal"
const char colorDelete[] = "#EE0000";  // cursor color "very visible"

int safeUC(int key);
void lowerCase(Byte *buf, int len);
char *pretty(char *buffer, FPos *size, int sign);
void exitMsg(int status, const char *message);
void positionInWin(Command cmd, short width, const char *title);

//--------------------------------------------------------------------
// Help screen text - max 21 lines (minScreenHeight - 3)   ##:x

const char *aHelp[] = {
"  ",
"  Move:  left right up down   home end    space backspace",
"  ",
"  Find   Next Prev       PgDn PgUp == next/prev diff byte",
"  ",
"  Goto [+-]{dec 0x x %}  last ' <  . ,   +4% + * =  -1% -",
"  ",
"  Edit file (overwrite),     show Raster,     Ignore case",
"  Quit/Esc,                any key interrupt the searches",
"  ",
"                      --- One File ---",
"  Enter == sm4rtscroll   Ascii mode",
"  ",
"                      --- Two Files ---",
"  Enter == next diff  # \\ == prev diff  1 2 == sync views",
"                           use only Top,  use only Bottom",
"  ",
"                      --- Edit ---",
"  Enter == copy byte from other file;     Insert   Ctrl-U",
"  Tab  ==  HEX <> ASCII, Esc == done;     Delete   Ctrl-K",
"  "
};

const int longestLine = 57;  // adjust!

const Byte aBold[] = {  // hotkeys, start y:1, x:1
        4,3,  4,10, 4,15,
        6,3,  6,31, 6,33,  6,36, 6,38,  6,46, 6,48, 6,50,  6,57,
        8,3,  8,35,  8,47,
        9,3,
        12,26,
        15,23, 15,25,  15,41, 15,43,
        16,37, 16,52,
        '\0' };
//--------------------------------------------------------------------

const int helpWidth = 1 + longestLine + 2 + 1;
const int helpHeight = 1 + sizeof(aHelp) / sizeof(aHelp[0]) + 1;

//====================================================================
// FileIO

bool stopRead;

inline File OpenFile(const char* path, bool writable=false)
{
  return open(path, (writable ? O_RDWR : O_RDONLY));
}

inline void CloseFile(File file)
{
  close(file);
}

bool WriteFile(File file, const void* buffer, Size count)
{
  const char* ptr = reinterpret_cast<const char*>(buffer);

  while (count > 0) {
    Size bytesWritten = write(file, ptr, count);
    if (bytesWritten < 1) {
      if (errno == EINTR)
        bytesWritten = 0;
      else
        return false;
    }

    ptr   += bytesWritten;
    count -= bytesWritten;
  } // end while more to write

  return true;
} // end WriteFile

inline int ReadFile(File file, void* buffer, Size count)
{
        int ret = read(file, buffer, count);

        /* interrupt the searches */
        timeout(0);
        switch(getch()) {
                case ERR:
                case KEY_UP:  // scrollwheel
                case KEY_DOWN:  break;
                default:  stopRead = true;
        }
        timeout(-1);

        return ret;
}

inline FPos SeekFile(File file, FPos position, int whence=SEEK_SET)
{
  return lseek(file, position, whence);
}

//====================================================================
// Class ConWindow   ##:win

class ConWindow
{
 protected:
  WINDOW        *winW;

 public:
                ConWindow()                             {}
               ~ConWindow()                             { closeW(); }

  static bool   startup();
  static void   shutdownW();

  void          initW(short x, short y, short width, short height, Style style);
  void          updateW()                               { touchwin(winW); wrefresh(winW); }
  int           readKeyW()                              { return wgetch(winW); }
  void          closeW()                                { if (winW) { delwin(winW); winW = NULL; } }

  void          put(short x, short y, const char* s)    { mvwaddstr(winW, y, x, s); }
  void          setAttribs(Style color)                 { wattrset(winW, attribStyle[color]); }
  void          putAttribs(short x, short y, Style color, short count);

  void          setCursor(short x, short y)             { wmove(winW, y, x); }
  static void   showCursor(bool insert=true);
  static void   hideCursor()                            { curs_set(0); }
}; // end ConWindow

//--------------------------------------------------------------------
// Start up the window system

bool ConWindow::startup()
{
  if (!initscr()) return false;  // initialize the curses library
  set_escdelay(10);              // for static linking
  atexit(ConWindow::shutdownW);  // just in case

  keypad(stdscr, true);         // enable keyboard mapping
  nonl();           // tell curses not to do NL->CR/NL on output
  cbreak();         // take input chars one at a time, no wait for \n
  noecho();         // do not echo input

  if (has_colors()) {
    start_color();

    init_pair(pairWhiteBlue,   COLOR_WHITE,  COLOR_BLUE);
    init_pair(pairWhiteBlack,  COLOR_WHITE,  COLOR_BLACK);
    init_pair(pairRedWhite,    COLOR_RED,    COLOR_WHITE);
    init_pair(pairYellowBlue,  COLOR_YELLOW, COLOR_BLUE);
    init_pair(pairGreenBlue,   COLOR_GREEN,  COLOR_BLUE);
    init_pair(pairBlackCyan,   COLOR_BLACK,  COLOR_CYAN);
    init_pair(pairGreenBlack,  COLOR_GREEN,  COLOR_BLACK);
    init_pair(pairWhiteCyan,   COLOR_WHITE,  COLOR_CYAN);
    init_pair(pairWhiteRed,    COLOR_WHITE,  COLOR_RED);
    init_pair(pairBlackYellow, COLOR_BLACK,  COLOR_YELLOW);
  } // end if terminal has color

  hideCursor();
  return true;
} // end ConWindow::startup

//--------------------------------------------------------------------
// Shut down the window system

void ConWindow::shutdownW()
{
  if (!isendwin()) {
    showCursor();
    endwin();
  }
}

//--------------------------------------------------------------------
// Initialize the window

void ConWindow::initW(short x, short y, short width, short height, Style attrib)
{
  if ((winW = newwin(height, width, y, x)) == 0)
    exitMsg(91, "Internal error: Failed to create window");

  wbkgd(winW, attribStyle[attrib]);

  keypad(winW, TRUE);
}

//--------------------------------------------------------------------
// Change the attributes of characters in the window

void ConWindow::putAttribs(short x, short y, Style color, short count)
{
  mvwchgat(winW, y, x, count, attribStyle[color], colorStyle[color], NULL);
}

//--------------------------------------------------------------------
// Visible difference between insert and overstrike mode

void ConWindow::showCursor(bool insert)
{
        insert ? curs_set(1) : curs_set(2);

#if SET_CURSOR_COLOR
        insert ? printf("\e]12;%s\a", colorInsert) : printf("\e]12;%s\a", colorDelete);

        fflush(stdout);
#endif
}

//====================================================================
// Class FileDisplay

 class Difference;

class FileDisplay  // ##:file
{
  friend class Difference;

  ConWindow             cwinF;

  const Difference     *diffsF;

  char                  fileName[maxPath];
  File                  fd;
  bool                  editable;  // file r/w
  bool                  writable;  // filehandle r/w

  Byte                 *dataF;
  int                   bufContents;
  FPos                  offset;
  FPos                  prevOffset;
  FPos                  diffOffset;
  FPos                  lastOffset;

  FPos                 *addr;
  int                   se4rch;

 public:
  FPos                  filesize;
  FPos                  searchOff;
  FPos                  scrollOff;
  FPos                  repeatOff;
  bool                  advance;
  bool                  two;

 public:
                FileDisplay()                           {}
               ~FileDisplay();

  bool          setFile(const char* aFileName);
  void          initF(int y, const Difference* aDiff);
  void          resizeF();
  void          updateF()                               { cwinF.updateW(); }
  int           readKeyF()                              { return cwinF.readKeyW(); }
  void          shutDownF()                             { cwinF.closeW(); }

  void          display();
  void          busy(bool on=false, bool ic=false);
  void          highEdit(short count);

  bool          edit(const FileDisplay* other);
  void          setByte(short x, short y, Byte b);

  void          setLast()                               { lastOffset = offset; }
  void          getLast()                               { FPos tmp = offset; moveTo(lastOffset); lastOffset = tmp; }
  void          skip(bool upwards=false);
  void          sync(const FileDisplay* other);

  void          move(FPos step)                         { moveTo(offset + step); }
  void          moveTo(FPos newOffset);
  void          moveToEnd();
  void          moveTo(const Byte* searchFor, int searchLen);
  void          moveToBack(const Byte* searchFor, int searchLen);

  void          seekNotChar(bool upwards=false);
  void          smartScroll();

}; // end FileDisplay

//====================================================================
// Class Difference

class Difference
{
 friend void FileDisplay::display();

 protected:
  Byte*         dataD;
  FileDisplay*  file1D;
  FileDisplay*  file2D;

 public:
  Difference(FileDisplay* aFile1, FileDisplay* aFile2): file1D(aFile1), file2D(aFile2) {}
 ~Difference() { delete [] dataD; }
  void resizeD();
  int  compute(Command cmd);
  void speedup(int way);
}; // end Difference

//====================================================================
// Class InputManager

class InputManager
{
 private:
  char*        buf;             // The editing buffer
  const char*  restrictChar;    // If non-NULL, only allow these chars
  StrDeq&      history;         // The history vector to use
  size_t       historyPos;      // The current offset into history[]
  String       cur;             // The current input line
  int          maxLen;          // The size of buf (not including NUL)
  int          len;             // The current length of the string
  int          i;               // The current cursor position
  bool         upcase;          // Force all characters to uppercase?
  bool         splitHex;        // Entering space-separated hex bytes?
  bool         insert;          // False for overstrike mode

 private:
  bool normalize(int pos);
  void useHistory(int delta);

 public:
  InputManager(char* aBuf, int aMaxLen, StrDeq& aHistory);
  bool run();

  void setCharacters(const char* aRestriction)  { restrictChar = aRestriction; }
  void setSplitHex(bool val)                    { splitHex = val; }
  void setUpcase(bool val)                      { upcase = val; }
}; // end InputManager

//====================================================================
// Global Variables   ##:vars

FileDisplay     file1, file2;

Difference      diffs(&file1, &file2);

WINDOW         *winInput,
               *winHelp;

const char      *program_name;  // Name under which this program was invoked

bool            singleFile;
bool            showRaster;
bool            sizeTera;
bool            modeAscii;
bool            ignoreCase;
int             haveDiff;
LockState       lockState;

String          lastSearch, lastSearchIgnCase;
StrDeq          hexSearchHistory, textSearchHistory, positionHistory;

// set dynamically for 16/24/32 byte width
int screenWidth;   // Number of columns in curses
int linesTotal;    // Number of lines in curses
int numLines;      // Number of lines of each file to display
int bufSize;       // Number of bytes of each file to display
int lineWidth;     // Number of bytes displayed per line
int lineWidthAsc;  // Number of bytes displayed per line ascii
int inWidth;       // Number of digits in input window
int leftMar;       // Starting column of hex display
int leftMar2;      // Starting column of ASCII display
int searchIndent;  // Lines of search result indentation
int steps[4];      // Number of bytes to move for each step

Byte bufFile1[sizeReadBuf];
Byte bufFile2[sizeReadBuf];

//====================================================================
// Class Difference member functions

void Difference::resizeD()
{
  if (dataD)
    delete [] dataD;

  dataD = new Byte[bufSize];
}

//--------------------------------------------------------------------
// Compute differences   ##:u

int Difference::compute(Command cmd)
{
        haveDiff = 0;
        memset(dataD, 0, bufSize);

        if (! file1D->bufContents) {
                file1.moveToEnd();
        }

        if (! file2D->bufContents) {
                file2.moveToEnd();
        }

        const Byte *buf1 = file1D->dataF;
        const Byte *buf2 = file2D->dataF;

        int size = min(file1D->bufContents, file2D->bufContents);

        int diff = 0;
        for (; diff < size; ++diff) {
                if (*(buf1++) != *(buf2++)) {
                        dataD[diff] = true;
                        haveDiff++;
                }
        }

        size = max(file1D->bufContents, file2D->bufContents);

        for (; diff < size; ++diff) {
                dataD[diff] = true;
                haveDiff++;
        }

        if (cmd == cmPrevDiff && (! file1D->offset || ! file2D->offset)) {
                return 1;
        }

        if (cmd == cmNextDiff && (file1D->bufContents < bufSize || file2D->bufContents < bufSize)) {
                haveDiff = -1;
        }

        return haveDiff;
} // end Difference::compute

//--------------------------------------------------------------------
// Speedup differ - diff in next/prev sizeReadBuf bytes

void Difference::speedup(int way) {
        if (way > 0) {
                SeekFile(file1D->fd, file1D->offset);
                SeekFile(file2D->fd, file2D->offset);

                while (file1D->offset + sizeReadBuf < file1.filesize &&
                                file2D->offset + sizeReadBuf < file2.filesize && ! stopRead) {
                        ReadFile(file1D->fd, bufFile1, sizeReadBuf);
                        ReadFile(file2D->fd, bufFile2, sizeReadBuf);

                        if (memcmp(bufFile1, bufFile2, sizeReadBuf)) {
                                break;
                        }

                        file1D->offset += sizeReadBuf;
                        file2D->offset += sizeReadBuf;
                }
        }
        else {  // downwards
                while (file1D->offset - sizeReadBuf > 0 &&
                                file2D->offset - sizeReadBuf > 0 && ! stopRead) {
                        SeekFile(file1D->fd, file1D->offset - sizeReadBuf);
                        SeekFile(file2D->fd, file2D->offset - sizeReadBuf);

                        ReadFile(file1D->fd, bufFile1, sizeReadBuf);
                        ReadFile(file2D->fd, bufFile2, sizeReadBuf);

                        if (memcmp(bufFile1, bufFile2, sizeReadBuf)) {
                                break;
                        }

                        file1D->offset -= sizeReadBuf;
                        file2D->offset -= sizeReadBuf;
                }
        }
} // end Difference::speedup

//====================================================================
// Class FileDisplay member functions

FileDisplay::~FileDisplay()
{
  shutDownF();
  CloseFile(fd);
  delete [] dataF;
  free(addr);
}

//--------------------------------------------------------------------
// Open a file for display

bool FileDisplay::setFile(const char* aFileName)
{
        strncpy(fileName, aFileName, maxPath - 1);
        fileName[maxPath - 1] = '\0';

        File e = OpenFile(fileName, true);
        if (e > InvalidFile) {
                editable = true;
                CloseFile(e);
        }

        fd = OpenFile(fileName);

        if (fd == InvalidFile)
                return false;

        filesize = SeekFile(fd, 0, SEEK_END);

        if (filesize > 1000000000000000000)
                return false;

        if (filesize > 68719476736)
                sizeTera = true;  // 2**30*64 == 0x10**9 == 64GB

        SeekFile(fd, 0);

        return true;
} // end FileDisplay::setFile

void FileDisplay::initF(int y, const Difference* aDiff)
{
  diffsF = aDiff;
  two = y ? true : false;

  cwinF.initW(0, y, screenWidth, numLines + 1, cFileWin);

  resizeF();

  addr = (FPos*) calloc(numLines, sizeof(FPos));
}

void FileDisplay::resizeF()
{
        if (dataF) delete [] dataF;
        dataF = new Byte[bufSize];

        moveTo(offset);
}

//--------------------------------------------------------------------
// Display the file contents   ##:disp

void FileDisplay::display()
{
        if (! fd) return;

        short first, last, row, col, idx, lineLength;
        FPos lineOffset = offset;

        if (scrollOff) {
                diffOffset = scrollOff - offset;
        }
        else if (offset != prevOffset) {
                diffOffset = offset - prevOffset;
                prevOffset = offset;
        }

        Byte pos = (scrollOff ? scrollOff + lineWidth : offset + bufSize) * 100 / filesize;

        char bufStat[screenWidth + 1] = { 0 };
        memset(bufStat, ' ', screenWidth);

        char buf[90], buf2[2][40];
        sprintf(buf, " %s %s %d%% %s %s", pretty(buf2[0], &offset, 0), pretty(buf2[1], &diffOffset, 1),
                pos > 100 ? 100 : pos, ignoreCase ? "I" : "i", editable ? "RW" : "RO");

        short size_name = screenWidth - strlen(buf);
        short size_fname = strlen(fileName);

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
        cwinF.putAttribs(0, 0, cFileName, strlen(bufStat));

        if (lockState == lockBottom && ! two) {
                cwinF.putAttribs(0, 0, cHighFile, size_name);
        }
        else if (lockState == lockTop && two) {
                cwinF.putAttribs(0, 0, cHighFile, size_name);
        }

        if (diffOffset < 0) {
                char *pc = (char*) memchr(buf, '-', strlen(buf));
                cwinF.putAttribs(size_name + (pc - buf), 0, cFileSearch, 1);
        }

        char bufHex[screenWidth + 1] = { 0 };
        char bufAsc[  lineWidth + 1] = { 0 };

        for (row=0; row < numLines; ++row) {
                memset(bufHex, ' ', screenWidth);
                memset(bufAsc, ' ',   lineWidth);

                if (*(addr + row)) {
                        lineOffset += (lineWidth * (*(addr + row)));
                }

                char *pbufHex = bufHex;
                pbufHex += sprintf(pbufHex, "%0*lX ", sizeTera ? 12 : 9, lineOffset);

                lineLength = min(lineWidth, bufContents - row * lineWidth);

                for (col = idx = 0; col < lineLength; ++col, ++idx) {
                        if (! col) {
                                *pbufHex++ = ' ';
                        }

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
                cwinF.putAttribs(col, row + 1, cFileAddr, (sizeTera ? 12 : 9) - col);

                if (showRaster) {
                        if (sizeTera) {
                                cwinF.putAttribs(0, row + 1, cFileMark, 1);
                        }
                        cwinF.putAttribs(sizeTera ? 4 : 1, row + 1, cFileMark, 1);
                        cwinF.putAttribs(sizeTera ? 8 : 5, row + 1, cFileMark, 1);
                }

                if (! modeAscii && showRaster && bufHex[leftMar] != ' ')
                        for (col=0; col <= lineWidth - 8; col += 8) {
                                cwinF.putAttribs(leftMar  + col * 3 - 1, row + 1, cFileMark, 1);
                                cwinF.putAttribs(leftMar2 + col        , row + 1, cFileMark, 1);
                        }

                if (haveDiff)
                        for (col=0; col < lineWidth; ++col)
                                if (diffsF->dataD[row * lineWidth + col]) {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cFileDiff, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cFileDiff, 1);
                                }

                if (se4rch && row >= (searchOff ? searchIndent / lineWidth : 0))
                        for (col=0; col < lineWidth && se4rch; ++col, --se4rch) {
                                if (modeAscii) {
                                        cwinF.putAttribs(leftMar  + col    , row + 1, cFileSearch, 1);
                                }
                                else {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cFileSearch, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cFileSearch, 1);
                                }
                        }

                if (*(addr + row))
                        for (col=0; col < lineWidth; ++col) {
                                if (modeAscii) {
                                        cwinF.putAttribs(leftMar  + col    , row + 1, cFileDiff, 1);
                                }
                                else {
                                        cwinF.putAttribs(leftMar  + col * 3, row + 1, cFileDiff, 2);
                                        cwinF.putAttribs(leftMar2 + col    , row + 1, cFileDiff, 1);
                                }
                        }
                lineOffset += lineWidth;
        } // end for row up to numLines

        if (scrollOff) {
                moveTo(offset);  // reload buffer
                memset(addr, 0, numLines * sizeof(FPos));
        }

        updateF();
} // end FileDisplay::display

//--------------------------------------------------------------------
// Busy status

void FileDisplay::busy(bool on, bool ic)
{
        if (on) {
                cwinF.putAttribs(screenWidth - (ic ? 4 : 2),  0, cHighBusy, ic ? 1 : 2);
                updateF();
        }
        else {
                napms(150);
                cwinF.putAttribs(screenWidth - (ic ? 4 : 2),  0, cFileName, ic ? 1 : 2);
        }
}

//--------------------------------------------------------------------
// Highlight statusbar

void FileDisplay::highEdit(short count)
{
        cwinF.putAttribs(0, 0, cHighEdit, count);
}

//--------------------------------------------------------------------
// Edit the file   ##:edit

bool FileDisplay::edit(const FileDisplay* other)
{
  if (!bufContents && offset)
    return false;               // You must not be completely past EOF

  if (!writable) {
    File w = OpenFile(fileName, true);
    if (w == InvalidFile) return false;
    CloseFile(fd);
    fd = w;
    writable = true;
  }

  if (bufContents < bufSize)
    memset(dataF + bufContents, 0, bufSize - bufContents);

  short x = 0;
  short y = 0;
  bool  hiNib = true;
  bool  ascii = false;
  bool  changed = false;
  int   key;

  cwinF.setCursor(leftMar,1);
  ConWindow::showCursor();

  for (;;) {
    cwinF.setCursor((ascii ? leftMar2 + x : leftMar + 3*x + !hiNib), y+1);
    key = readKeyF();

    switch (key) {
     case KEY_ESCAPE: goto done;
     case KEY_TAB:
      hiNib = true;
      ascii = !ascii;
      break;

     case KEY_DELETE:
     case KEY_BACKSPACE:
     case KEY_LEFT:
      if (!hiNib)
        hiNib = true;
      else {
        if (!ascii) hiNib = false;
        if (--x < 0) x = lineWidth-1;
      }
      if (hiNib || (x < lineWidth-1))
        break;
      // else fall thru
     case KEY_UP:   if (--y < 0) y = numLines-1; break;

     default: {
       short newByte = -1;
       if ((key == KEY_RETURN) && other &&
           (other->bufContents > x + y*lineWidth)) {
         newByte = other->dataF[y * lineWidth + x]; // Copy from other file
         hiNib = ascii; // Always advance cursor to next byte
       } else if (ascii) {
         if (isprint(key)) newByte = key;
       } else { // hex
         if (isdigit(key))
           newByte = key - '0';
         else if (isxdigit(key))
           newByte = safeUC(key) - 'A' + 10;
         if (newByte >= 0) {
           if (hiNib)
             newByte = (newByte * 0x10) | (0x0F & dataF[y * lineWidth + x]);
           else
             newByte |= 0xF0 & dataF[y * lineWidth + x];
         } // end if valid digit entered
       } // end else hex
       if (newByte >= 0) {
         changed = true;
         setByte(x,y,newByte);
       } else
         break;
     } // end default and fall thru
     case KEY_RIGHT:
      if (hiNib && !ascii)
        hiNib = false;
      else {
        hiNib = true;
        if (++x >= lineWidth) x = 0;
      }
      if (x || !hiNib)
        break;
      // else fall thru
     case KEY_DOWN: if (++y >= numLines) y = 0;  break;

    } // end switch

  } // end forever

 done:
  if (changed) {
    positionInWin(two ? cmgGotoBottom : cmgGotoTop, 25, "");
    mvwaddstr(winInput, 1, 1, " Save changes (Y/N): ");
    key = wgetch(winInput);
    ConWindow::hideCursor();

    if (safeUC(key) != 'Y') {
      changed = false;
      moveTo(offset);           // Re-read buffer contents
    } else {
      SeekFile(fd, offset);
      if (WriteFile(fd, dataF, bufContents)) {
        mvwaddstr(winInput, 1, 1, "        Success.     ");
        wrefresh(winInput);
        napms(1500);
      } else {
        mvwaddstr(winInput, 1, 1, "        Failed!      ");
        wgetch(winInput);
      }
    }
  } else {
    ConWindow::hideCursor();
  }
  return changed;
} // end FileDisplay::edit

//--------------------------------------------------------------------
// FileDisplay::setByte

void FileDisplay::setByte(short x, short y, Byte b)
{
  if (x + y*lineWidth >= bufContents) {
    if (x + y*lineWidth > bufContents) {
      short y1 = bufContents / lineWidth;
      short x1 = bufContents % lineWidth;
      while (y1 <= numLines) {
        while (x1 < lineWidth) {
          if ((x1 == x) && (y1 == y)) goto done;
          setByte(x1,y1,0);
          ++x1;
        }
        x1 = 0;
        ++y1;
      } // end while y1
    } // end if more than 1 byte past the end
   done:
    ++bufContents;
    dataF[y * lineWidth + x] = b ^ 1;         // Make sure it's different
  } // end if past the end

  if (dataF[y * lineWidth + x] != b) {
    dataF[y * lineWidth + x] = b;
    char str[3];
    sprintf(str, "%02X", b);
    cwinF.setAttribs(cFileEdit);
    cwinF.put(leftMar + 3*x, y+1, str);
    str[0] = b >= 0x20 && b <= 0x7E ? b : '.';
    str[1] = '\0';
    cwinF.put(leftMar2 + x, y+1, str);
    cwinF.setAttribs(cFileWin);
  }
} // end FileDisplay::setByte

//--------------------------------------------------------------------
// Jump a specific percentage forward / backward

void FileDisplay::skip(bool upwards)
{
        FPos step = filesize / 100;

        if (upwards)
                move(step * -skipBack);
        else
                move(step * skipForw);
}

//--------------------------------------------------------------------
// Synchronize the plains
//
// '1' | upper:  sync file1 with file2
// '2' | lower:  sync file2 with file1

void FileDisplay::sync(const FileDisplay* other)
{
        if (other->bufContents) {
                moveTo(other->offset);
        }
        else {
                moveToEnd();
        }
}

//--------------------------------------------------------------------
// Change the file position   ##:to

void FileDisplay::moveTo(FPos newOffset)
{
        offset = newOffset;

        if (offset < 0) {
                offset = 0;
        }
        if (offset > filesize) {
                offset = filesize;
        }

        SeekFile(fd, offset);

        bufContents = ReadFile(fd, dataF, bufSize);
}

//--------------------------------------------------------------------
// Move to the end of the file

void FileDisplay::moveToEnd()
{
        FPos end = filesize - steps[cmmMovePage];

        moveTo(end);
}

//--------------------------------------------------------------------
// Change the file position by searching

void FileDisplay::moveTo(const Byte* searchFor, int searchLen)
{
        if (stopRead) return;

        const int blockSize = 2 * 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize + searchLen];

        FPos newPos = searchOff ? searchOff : offset;
        if (advance) ++newPos;
        SeekFile(fd, newPos);
        int bytesRead = ReadFile(fd, searchBuf + searchLen, blockSize);

        if (ignoreCase) { lowerCase(searchBuf + searchLen, bytesRead); }

        for (int l = searchLen; bytesRead > 0; l = 0) {  // adjust for first block
                for (int i=0; i <= bytesRead - l; ++i) {
                        if (*searchFor == searchBuf[l + i]) {
                                if (! memcmp(searchFor, searchBuf + l + i, searchLen)) {
                                        delete [] searchBuf;
                                        newPos = newPos + i - searchLen + l;
                                        if (searchIndent && newPos >= searchIndent) {
                                                searchOff = newPos;
                                                moveTo(newPos - searchIndent);
                                        }
                                        else {
                                                searchOff = 0;
                                                moveTo(newPos);
                                        }
                                        se4rch = searchLen;
                                        advance = true;
                                        return;
                                }
                        }
                }
                if (stopRead) { break; }

                newPos += blockSize;
                memcpy(searchBuf, searchBuf + blockSize, searchLen);
                bytesRead = ReadFile(fd, searchBuf + searchLen, blockSize);
                if (ignoreCase) { lowerCase(searchBuf + searchLen, bytesRead); }
        }
        advance = false;
        delete [] searchBuf;

        stopRead ? moveTo(newPos) : moveToEnd();
} // end FileDisplay::moveTo

//--------------------------------------------------------------------
// Change the file position by searching backward

void FileDisplay::moveToBack(const Byte* searchFor, int searchLen)
{
        if (stopRead) return;

        const int blockSize = 8 * 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize + searchLen];
        FPos newPos = (searchOff ? searchOff : offset) - blockSize;
        memcpy(searchBuf + blockSize, dataF + (searchOff ? searchIndent : 0), searchLen);
        int diff = 0, bytesRead;

        for (int l=advance ? 0 : 1; ; l=0) {
                if (newPos < 0) { diff = newPos; newPos = 0; }
                SeekFile(fd, newPos);
                if ((bytesRead = ReadFile(fd, searchBuf, blockSize)) <= 0) break;

                if (ignoreCase) { lowerCase(searchBuf, bytesRead); }

                if (diff) memmove(searchBuf + (blockSize + diff), searchBuf + blockSize, searchLen);

                for (int i = blockSize - 1 + diff + l; i >= 0; --i) {
                        if (*searchFor == searchBuf[i]) {
                                if (! memcmp(searchFor, searchBuf + i, searchLen)) {
                                        delete [] searchBuf;
                                        newPos = newPos + i;
                                        if (searchIndent && newPos >= searchIndent) {
                                                searchOff = newPos;
                                                moveTo(newPos - searchIndent);
                                        }
                                        else { searchOff = 0; moveTo(newPos); }
                                        se4rch = searchLen;
                                        advance = true;
                                        return;
                                }
                        }
                }
                if (! newPos || stopRead) break;

                memcpy(searchBuf + blockSize, searchBuf, searchLen);
                newPos -= blockSize;
        }
        advance = false;
        delete [] searchBuf;
        moveTo(stopRead ? newPos : 0);
} // end FileDisplay::moveToBack

//--------------------------------------------------------------------
// Seek to next byte not equal to current head

void FileDisplay::seekNotChar(bool upwards)
{
        if (stopRead) return;

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

                if (! bufContents) {
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
        if (modeAscii)
                for (int k=0; k < lineWidth; ++k)
                        if (! isprint(dataF[k]))
                                dataF[k] = ' ';
        bytesRead -= lineWidth;

        int i = 1, j = 1;
        for ( ; bytesRead > 0; ) {
                if (bytesRead >= lineWidth) {
                        memcpy(buf, scrollBuf + j * lineWidth, lineWidth);

                        if (modeAscii)
                                for (int k=0; k < lineWidth; ++k)
                                        if (! isprint(buf[k]))
                                                buf[k] = ' ';

                        if (memcmp(dataF + (i - 1) * lineWidth, buf, lineWidth)) {
                                memcpy(dataF + i * lineWidth, buf, lineWidth);

                                *(addr + i) = repeat;
                                repeat = 0;
                                ++i;

                        } else {
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

                        } else {
                                if (repeat) {
                                        *(addr + i) = --repeat;

                                        memcpy(dataF + i * lineWidth, dataF + (i - 1) * lineWidth, lineWidth);
                                        ++i;
                                }
                        }
                }
        }

        scrollOff = newPos + j * lineWidth;

        bufContents = i * lineWidth + min(bytesRead, lineWidth);

        delete [] scrollBuf;
} // end FileDisplay::smartScroll

//====================================================================
// Class InputManager member functions

InputManager::InputManager(char* aBuf, int aMaxLen, StrDeq& aHistory)
: buf(aBuf),
  restrictChar(NULL),
  history(aHistory),
  historyPos(aHistory.size()),
  maxLen(aMaxLen),
  len(0),
  i(0),
  upcase(false),
  splitHex(false),
  insert(true)
{
}

//--------------------------------------------------------------------
// Normalize the string in the input window

bool InputManager::normalize(int pos)
{
  if (!splitHex) return false;

  // Change D_ to 0D:
  if (pos && buf[pos] == ' ' && buf[pos-1] != ' ') {
    buf[pos] = buf[pos-1];
    buf[pos-1] = '0';
    if (pos == len) len += 2;
    return true;
  }

  // Change _D to 0D:
  if (pos < len && buf[pos] == ' ' && buf[pos+1] != ' ') {
    buf[pos] = '0';
    return true;
  }

  return false;                 // No changes necessary
}

//--------------------------------------------------------------------
// Switch the current input line with one from the history
//
// Input:
//   delta:  The number to add to historyPos (-1 previous, +1 next)

void InputManager::useHistory(int delta)
{
  // Clean up the current string if necessary:
  normalize(i);

  // Save the current string
  if (historyPos == history.size()) {
    cur.assign(buf, len);
  }

  historyPos += delta;

  String s = historyPos == history.size() ? cur : history[historyPos];

  // Store the new string in the buffer:
  memset(buf, ' ', maxLen);
  i = len = min(maxLen, (int) s.length());
  memcpy(buf, s.c_str(), len);
} // end InputManager::useHistory

//--------------------------------------------------------------------
// Run the main loop to get an input string

bool InputManager::run()
{
  wmove(winInput, 1, 2);

  bool  done   = false;
  bool  aborted = true;

  ConWindow::showCursor(insert);

  memset(buf, ' ', maxLen);
  buf[maxLen] = '\0';

  // We need to be able to display complete bytes:
  if (splitHex && (maxLen % 3 == 1)) --maxLen;

  // Main input loop:
  while (!done) {
    mvwaddstr(winInput, 1, 2, buf);
    wmove(winInput, 1, 2 + i);
    int key = wgetch(winInput);
    if (upcase) key = safeUC(key);

    switch (key) {
     case KEY_ESCAPE:  buf[0] = '\0';  done = true;  break; // ESC

     case KEY_RETURN:           // Enter
      normalize(i);
      buf[len] = '\0';
      done = true;
      aborted = false;
      break;

     case KEY_BACKSPACE:
     case KEY_DELETE:           // Backspace on most Unix terminals
     case 0x08:                 // Backspace (Ctrl-H)
      if (!i) continue; // Can't back up if we're at the beginning already
      historyPos = history.size();
      if (splitHex) {
        if ((i % 3) == 0) {
          // At the beginning of a byte; erase last digit of previous byte:
          if (i == len) len -= 2;
          i -= 2;
          buf[i] = ' ';
        } else if (i < len && buf[i] != ' ') {
          // On the second digit; erase the first digit:
          buf[--i] = ' ';
        } else {
          // On a blank second digit; delete the entire byte:
          buf[--i] = ' ';
          memmove(buf + i, buf + i + 3, maxLen - i - 3);
          len -= 3;
          if (len < i) len = i;
        }
      } else { // not splitHex mode
        memmove(buf + i - 1, buf + i, maxLen - i);
        buf[maxLen-1] = ' ';
        --len;  --i;
      } // end else not splitHex mode
      break;

     case 0x04:                 // Ctrl-D
     case KEY_DC:
      if (i >= len) continue;
      historyPos = history.size();
      if (splitHex) {
        i -= i%3;
        memmove(buf + i, buf + i + 3, maxLen - i - 3);
        len -= 3;
        if (len < i) len = i;
      } else {
        memmove(buf + i, buf + i + 1, maxLen - i - 1);
        buf[maxLen-1] = ' ';
        --len;
      } // end else not splitHex mode
      break;

     case KEY_IC:
      insert = !insert;
      ConWindow::showCursor(insert);
      break;

     case 0x02:                 // Ctrl-B
     case KEY_LEFT:
      if (i) {
        --i;
        if (splitHex) {
          normalize(i+1);
          if (i % 3 == 2) --i;
        }
      }
      break;

     case 0x06:                 // Ctrl-F
     case KEY_RIGHT:
      if (i < len) {
        ++i;
        if (splitHex) {
          normalize(i-1);
          if ((i < maxLen) && (i % 3 == 2)) ++i;
        }
      }
      break;

     case 0x15:                 // Ctrl-U
      if (i) {
        historyPos = history.size();
        if (splitHex) {
          i -= i % 3;
        }
        int tail = len - i;
        memmove(buf, buf + i, tail);
        memset(buf + tail, ' ', maxLen - tail);
        len = tail;
        i = 0;
      }
      break;

     case 0x0B:                 // Ctrl-K
      if (len > i) {
        historyPos = history.size();
        if (splitHex) {
          i -= i % 3;
        }
        memset(buf + i, ' ', len - i);
        len = i;
      }
      break;

     case 0x01:                 // Ctrl-A
     case KEY_HOME:
      normalize(i);
      i = 0;
      break;

     case 0x05:                 // Ctrl-E
     case KEY_END:
      if (splitHex && (i < len))
        normalize(i);
      i = len;
      break;

     case 0x10:                 // Ctrl-P
     case KEY_UP:
      if (historyPos)
        useHistory(-1);
      break;

     case 0x0E:                 // Ctrl-N
     case KEY_DOWN:
      if (historyPos < history.size())
        useHistory(+1);
      break;

     default:
      if (isprint(key) && (!restrictChar || strchr(restrictChar, key))) {
        if (insert) {
          if (splitHex) {
            if (buf[i] == ' ') {
              if (i >= maxLen) continue;
            } else {
              if (len >= maxLen) continue;
              i -= i % 3;
              memmove(buf + i + 3, buf + i, maxLen - i - 3);
              buf[i+1] = ' ';
              len += 3;
            }
          } // end if splitHex mode
          else {
            if (len >= maxLen) continue;
            memmove(buf + i + 1, buf + i, maxLen - i - 1);
            ++len;
          } // end else not splitHex mode
        } else { // overstrike mode
          if (i >= maxLen) continue;
        } // end else overstrike mode
        historyPos = history.size();
        buf[i++] = key;
        if (splitHex && (i < maxLen) && (i % 3 == 2))
          ++i;
        if (i > len) len = i;
      } // end if is acceptable character to insert
    } // end switch key
  } // end while not done

  // Hide the input window & cursor:
  ConWindow::hideCursor();

  // Record the result in the history:
  if (!aborted && len) {
    auto exists = history.begin();

    for (; exists != history.end(); ++exists) {
            if (*exists == buf) break;
    }
    if (exists != history.end()) {
      history.erase(exists);
    }
    if (history.size() == maxHistory) {
      history.pop_front();
    }
    history.push_back(buf);
  } // end if we have a value to store in the history

  return !aborted;
} // end InputManager::run

//====================================================================
// Global Functions

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

void calcScreenLayout()  // ##:y
{
        if (COLS < minScreenWidth) {
                string err("The screen must be at least " + to_string(minScreenWidth) + " characters wide.");

                exitMsg(2, err.c_str());
        }

        if (LINES < minScreenHeight) {
                string err("The screen must be at least " + to_string(minScreenHeight) + " lines high.");

                exitMsg(3, err.c_str());
        }

        // use large addresses only if needed
        short tera = sizeTera ? 3 : 0;

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

        numLines = LINES / (singleFile ? 1 : 2) - 1;

        setViewMode();
} // end calcScreenLayout

//--------------------------------------------------------------------
// Convert a character to uppercase

int safeUC(int c)
{
  return (c >= 0 && c <= UCHAR_MAX) ? toupper(c) : c;
}

//--------------------------------------------------------------------
// Convert buffer to lowercase

void lowerCase(Byte *buf, int len)
{
        for (int i=0; i < len; ++i) {
                if (buf[i] >= 'A' && buf[i] <= 'Z') {
                        buf[i] |= 0x20;
                }
        }
}

//--------------------------------------------------------------------
// my pretty printer

char *pretty (char *buffer, FPos *size, int sign)
{
        char aBuf[50];
        char *pa = aBuf, *pb = buffer;

        if (sign)
                sprintf (aBuf, "%+ld", *size);
        else
                sprintf (aBuf, "%ld", *size);

        int len = strlen (aBuf);

        while (len) {
                *pb++ = *pa++;

                if (sign) { --sign; --len; }

                else if (--len && ! (len % 3))
                        if (thouSep)
                                *pb++ = thouSep;
        }
        *pb = '\0';

        return buffer;
}

//--------------------------------------------------------------------
// Get a string using winInput

void getString(char* buf, int maxLen, StrDeq& history,
               const char* restrictChar=NULL,
               bool upcase=false, bool splitHex=false)
{
  InputManager  manager(buf, maxLen, history);

  manager.setCharacters(restrictChar);
  manager.setSplitHex(splitHex);
  manager.setUpcase(upcase);

  manager.run();
}

//--------------------------------------------------------------------
// Move help window in stack

void displayHelp()
{
        touchwin(winHelp);
        wrefresh(winHelp);
        wgetch(winHelp);
}

//--------------------------------------------------------------------
// Print a message to stderr and exit

void exitMsg(int status, const char* message)
{
  ConWindow::shutdownW();

  fprintf(stderr, "\n %s\n", message);
  exit(status);
}

//--------------------------------------------------------------------
// Convert hex string to bytes

int packHex(Byte* buf)
{
  FPos val;

  char* in  = reinterpret_cast<char*>(buf);
  Byte* out = buf;

  while (*in) {
    if (*in == ' ')
      ++in;
    else {
      val = strtoull(in, &in, 16);
      *(out++) = Byte(val);
    }
  }

  return out - buf;
}

//--------------------------------------------------------------------
// Position the input window

void positionInWin(Command cmd, short width, const char* title)
{
        if (wresize(winInput, 3, width) != OK) {
                exitMsg(94, "Internal error: Failed to resize window");
        }

        wbkgd(winInput, attribStyle[cWindowBold]);
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

//--------------------------------------------------------------------
// Initialize program   ##:i

void initialize()
{
        calcScreenLayout();  // global vars

        if ((winInput = newwin(3, inWidth, 0, 0)) == 0) {
                exitMsg(92, "Internal error: Failed to create window");
        }
        keypad(winInput, true);

        if ((winHelp = newwin(helpHeight, helpWidth,
                              1 + (linesTotal - helpHeight) / 3,
                              1 + (screenWidth - helpWidth) / 2)) == 0) {
                exitMsg(93, "Internal error: Failed to create window");
        }

        wbkgd(winHelp, attribStyle[cWindowBold]);
        box(winHelp, 0, 0);

        mvwaddstr(winHelp, 0, (helpWidth - 6) / 2, " Help ");
        mvwaddstr(winHelp, helpHeight - 1, (helpWidth - 20 - (3+1) - 1) / 2, " VBinDiff for Linux " VBL_VERSION " ");

        for (int i=0; i < helpHeight - 2; ++i) {  // exclude border
                mvwaddstr(winHelp, i + 1, 1, aHelp[i]);
        }

        for (int i=0; aBold[i]; i += 2) {
                mvwchgat(winHelp, aBold[i], aBold[i + 1], 1, attribStyle[cHotKey], colorStyle[cHotKey], NULL);
        }

        if (! singleFile)
                diffs.resizeD();

        file1.initF(0, (singleFile ? NULL : &diffs));

        if (! singleFile)
                file2.initF(numLines + 1, &diffs);
} // end initialize

//--------------------------------------------------------------------
// Get a command from the keyboard   ##:get

Command getCommand()
{
        Command cmd = cmNothing;

        while (cmd == cmNothing) {
                int e = file1.readKeyF();

                switch (safeUC(e)) {
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
                        case '<':  cmd = cmgGoto | cmgGotoLast; break;
                        case '.':  cmd = cmgGoto | cmgGotoLOff; break;
                        case ',':  cmd = cmgGoto | cmgGotoNOff; break;

                        case 'T':  if (! singleFile) cmd = cmUseTop; break;
                        case 'B':  if (! singleFile) cmd = cmUseBottom; break;

                        case KEY_RETURN:
                                if (singleFile)
                                        cmd = cmSmartScroll;
                                else
                                        cmd = cmNextDiff;
                                break;

                        case '#':
                        case '\\':  if (! singleFile) cmd = cmPrevDiff; break;

                        case 'E':
                                if (lockState == lockTop)
                                        cmd = cmEditBottom;
                                else
                                        cmd = cmEditTop;
                                break;

                        case '1':  if (! singleFile) cmd = cmSyncUp; break;
                        case '2':  if (! singleFile) cmd = cmSyncDn; break;

                        case 'A':  if (singleFile) cmd = cmShowAscii; break;

                        case 'I':  cmd = cmIgnoreCase; break;

                        case 'R':  cmd = cmShowRaster; break;

                        case 'H':  cmd = cmShowHelp; break;

                        case KEY_ESCAPE:
                        case KEY_CTRL_C:
                        case 'Q':  cmd = cmQuit; break;
                } // end switch ASCII code
        } // end while no command

        if (cmd & (cmmMove | cmfFind | cmgGoto)) {
                if (lockState != lockTop)
                        cmd |= cmgGotoTop;

                if (lockState != lockBottom && ! singleFile)
                        cmd |= cmgGotoBottom;
        }

        return cmd;
} // end getCommand

//--------------------------------------------------------------------
// Get a file position and move there   ##:p

void gotoPosition(Command cmd)
{
        positionInWin(cmd, inWidth + 1 + 4, " Goto ");  // cursor + border

        char buf[inWidth + 1];

        getString(buf, inWidth, positionHistory, hexDigits, true);
        if (! buf[0]) return;

        FPos pos1 = 0, pos2 = 0;

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
        else if (strpbrk(buf, "ABCDEFX")) {
              pos1 = pos2 = strtoull(buf, NULL, 16);
        }
        else {
              pos1 = pos2 = strtoull(buf, NULL, 10);
        }

        if (cmd & cmgGotoTop) {
                file1.setLast();

                if (rel) {
                        file1.repeatOff = rel > 0 ? pos1 : -pos1;
                        file1.move(file1.repeatOff);
                }
                else {
                        file1.moveTo(pos1);
                }
        }
        if (cmd & cmgGotoBottom) {
                file2.setLast();

                if (rel) {
                        file2.repeatOff = rel > 0 ? pos2 : -pos2;
                        file2.move(file2.repeatOff);
                }
                else {
                        file2.moveTo(pos2);
                }
        }
} // end gotoPosition

//--------------------------------------------------------------------
// Search for text or bytes in the files   ##:s

void searchFiles(Command cmd)
{
        const bool havePrev = !lastSearch.empty();
        int key = 0;

        if (! ((cmd & cmfFindNext || cmd & cmfFindPrev) && havePrev)) {
                positionInWin(cmd, (havePrev ? 36 : 18), " Find ");

                mvwaddstr(winInput, 1,  2, "H Hex");
                mvwaddstr(winInput, 1, 10, "T Text");
                mvwchgat(winInput, 1,  2, 1, attribStyle[cHotKey], colorStyle[cHotKey], NULL);
                mvwchgat(winInput, 1, 10, 1, attribStyle[cHotKey], colorStyle[cHotKey], NULL);

                if (havePrev) {
                        mvwaddstr(winInput, 1, 19, "N Next");
                        mvwaddstr(winInput, 1, 28, "P Prev");
                        mvwchgat(winInput,  1, 19, 1, attribStyle[cHotKey], colorStyle[cHotKey], NULL);
                        mvwchgat(winInput,  1, 28, 1, attribStyle[cHotKey], colorStyle[cHotKey], NULL);
                }
                key = safeUC(wgetch(winInput));

                bool hex = false;

                if (key == KEY_ESCAPE) {
                        return;
                }
                else if (key == 'H') {
                        hex = true;
                }

                if (! ((key == 'N' || key == 'P') && havePrev)) {
                        positionInWin(cmd, screenWidth, (hex ? " Find Hex Bytes " : " Find Text "));

                        int maxLen = screenWidth - 4;
                        maxLen -= maxLen % 3;

                        char buf[maxLen + 1];
                        int searchLen;

                        if (hex) {
                                getString(buf, maxLen, hexSearchHistory, hexDigits, true, true);

                                searchLen = packHex(reinterpret_cast<Byte*>(buf));
                        }
                        else {
                                getString(buf, maxLen, textSearchHistory);

                                searchLen = strlen(buf);
                        }

                        if (! searchLen) return;

                        lastSearch.assign(buf, searchLen);

                        lowerCase((Byte*)buf, searchLen);

                        lastSearchIgnCase.assign(buf, searchLen);

                        file1.advance = file2.advance = false;
                }

                if (! singleFile) {
                        file2.updateF();  // kick remnants
                }
        } // end no direct N or P

        const Byte *const searchPattern =
                reinterpret_cast<const Byte*>((ignoreCase ? lastSearchIgnCase.c_str() : lastSearch.c_str()));

        if (cmd & cmfFindPrev || key == 'P') {
                if (cmd & cmgGotoTop) {
                        file1.busy(true);

                        file1.moveToBack(searchPattern, lastSearch.length());
                        file1.busy();
                }
                if (cmd & cmgGotoBottom) {
                        file2.busy(true);

                        file2.moveToBack(searchPattern, lastSearch.length());
                        file2.busy();
                }
        }
        else {
                if (cmd & cmgGotoTop) {
                        file1.busy(true);

                        file1.moveTo(searchPattern, lastSearch.length());
                        file1.busy();
                }
                if (cmd & cmgGotoBottom) {
                        file2.busy(true);

                        file2.moveTo(searchPattern, lastSearch.length());
                        file2.busy();
                }
        }
} // end searchFiles

//--------------------------------------------------------------------
// Handle a command   ##:hand

void handleCmd(Command cmd)
{
        stopRead = false;

        if (cmd & cmgGoto) {
                if (cmd & cmgGotoNOff) {
                        if (cmd & cmgGotoTop) {
                                file1.move(-file1.repeatOff);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.move(-file2.repeatOff);
                        }
                }
                else if (cmd & cmgGotoLOff) {
                        if (cmd & cmgGotoTop) {
                                file1.move(file1.repeatOff);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.move(file2.repeatOff);
                        }
                }
                else if (cmd & cmgGotoLast) {
                        if (cmd & cmgGotoTop) {
                                file1.getLast();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.getLast();
                        }
                }
                else if (cmd & cmgGotoForw) {
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
                int step = steps[cmd & cmmMoveSize];

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
                if (lockState == lockTop)
                        lockState = lockNeither;
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
                napms(800);  // Esc: avoid quit
                flushinp();
        }
} // end handleCmd

//====================================================================
// Main Program   ##:main

int main(int argc, const char* argv[])
{
        if ((program_name = strrchr(argv[0], '/')))
                ++program_name;
        else
                program_name = argv[0];

        printf("VBinDiff for Linux %s\n", VBL_VERSION);

        if (argc < 2 || argc > 3) {
                printf("\n\t%s file1 [file2]\n"
                        "\n"
                        "// type 'h' for help\n"
                        "\n", program_name);
                exit(0);
        }
        singleFile = (argc == 2);

        if (! ConWindow::startup()) {
                fprintf(stderr, "\n %s: Unable to initialize windows\n", program_name);
                return 1;
        }

        string err;

        if (! file1.setFile(argv[1])) {
                err = string("Unable to open ") + argv[1] + ": " + strerror(errno);
        }
        else if (! singleFile && ! file2.setFile(argv[2])) {
                err = string("Unable to open ") + argv[2] + ": " + strerror(errno);
        }
        else if (! file1.filesize) {
                err = string("File is empty: ") + argv[1];
        }
        else if (! singleFile && ! file2.filesize) {
                err = string("File is empty: ") + argv[2];
        }
        else if (file1.filesize > 281474976710656) {  // 2**40*256 == 0x10**12 == 256TB
                err = string("File is too big: ") + argv[1];
        }
        else if (! singleFile && file2.filesize > 281474976710656) {
                err = string("File is too big: ") + argv[2];
        }

        if (err.size())
                exitMsg(1, err.c_str());

        initialize();

        file1.display();
        file2.display();

        Command  cmd;
        while ((cmd = getCommand()) != cmQuit) {
                if (! (cmd & cmfFind && ! (cmd & (cmfNotCharDn | cmfNotCharUp)))) {
                        file1.searchOff = file2.searchOff = 0;
                        file1.advance = file2.advance = false;
                }

                if (! (cmd == cmNextDiff || cmd == cmPrevDiff)) {
                        haveDiff = 0;
                }

                if (cmd != cmSmartScroll) {
                        file1.scrollOff = 0;
                }
                handleCmd(cmd);
        }

        file1.shutDownF();
        file2.shutDownF();

        delwin(winInput);
        delwin(winHelp);

        ConWindow::shutdownW();

        return 0;
} // end main

