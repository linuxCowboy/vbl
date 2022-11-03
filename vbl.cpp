//--------------------------------------------------------------------
//
//   VBinDiff for Linux
//
//   Copyright 1995-2017 by Christopher J. Madsen
//   Copyright 2021-2022 by linuxCowboy
//
//   64GB by Bradley Grainger
//
//   Visual display of differences in binary files
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

#include <algorithm>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>

#include <panel.h>

using namespace std;

#define VBL_VERSION     "1.3"

#define KEY_CTRL_C      0x03
#define KEY_TAB         0x09
#define KEY_RETURN      0x0D
#define KEY_ESCAPE      0x1B
#define KEY_DELETE      0x7F

enum Style {
  cBackground,
  cPromptWin,
  cPromptKey,
  cPromptBdr,
  cCurrentMode,
  cFileName,
  cFileWin,
  cFileDiff,
  cFileEdit,
  cFileSearch,
  cFileMark,
  cFileAddr,
  cHotKey
};

enum ColorPair {
  pairWhiteBlue = 1,
  pairWhiteBlack,
  pairRedWhite,
  pairYellowBlue,
  pairGreenBlue,
  pairBlackCyan,
  pairGreenBlack
};

static const ColorPair colorStyle[] = {
  pairWhiteBlue,   // cBackground
  pairWhiteBlue,   // cPromptWin
  pairWhiteBlue,   // cPromptKey
  pairWhiteBlue,   // cPromptBdr
  pairWhiteBlack,  // cCurrentMode
  pairWhiteBlack,  // cFileName
  pairWhiteBlue,   // cFileWin
  pairGreenBlack,  // cFileDiff
  pairYellowBlue,  // cFileEdit
  pairRedWhite,    // cFileSearch
  pairBlackCyan,   // cFileMark
  pairYellowBlue,  // cFileAddr
  pairGreenBlue    // cHotKey
};

static const attr_t attribStyle[] = {
              COLOR_PAIR(colorStyle[ cBackground ]),
              COLOR_PAIR(colorStyle[ cPromptWin  ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cPromptKey  ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cPromptBdr  ]),
  A_REVERSE | COLOR_PAIR(colorStyle[ cCurrentMode]),
  A_REVERSE | COLOR_PAIR(colorStyle[ cFileName   ]),
              COLOR_PAIR(colorStyle[ cFileWin    ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileDiff   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileEdit   ]),
              COLOR_PAIR(colorStyle[ cFileSearch ]),
              COLOR_PAIR(colorStyle[ cFileMark   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cFileAddr   ]),
  A_BOLD    | COLOR_PAIR(colorStyle[ cHotKey     ])
};

//====================================================================
// Type definitions

typedef unsigned char   Byte;
typedef Byte  Command;

typedef int      File;
typedef off_t    FPos;  // long int
typedef ssize_t  Size;  // long int

enum LockState { lockNeither, lockTop, lockBottom };

//--------------------------------------------------------------------
// Strings

typedef string  String;

typedef String::size_type      StrIdx;
typedef String::iterator       StrItr;
typedef String::const_iterator StrConstItr;

//--------------------------------------------------------------------
// Vectors

typedef vector<String>          StrVec;
typedef StrVec::iterator        SVItr;
typedef StrVec::const_iterator  SVConstItr;

typedef StrVec::size_type  VecSize;

//--------------------------------------------------------------------
// Map

typedef map<VecSize, String>    StrMap;
typedef StrMap::value_type      SMVal;
typedef StrMap::iterator        SMItr;
typedef StrMap::const_iterator  SMConstItr;

//====================================================================
// Constants   :##cmd

const Command  cmmMove        = 0x80;  // Main cmd
const Command  cmmMoveForward = 0x40;
const Command  cmmMoveByte    = 0x00;  // Move 1 byte
const Command  cmmMoveLine    = 0x01;  // Move 1 line
const Command  cmmMovePage    = 0x02;  // Move 1 page
const Command  cmmMoveAll     = 0x03;  // Move to beginning or end
const Command  cmmMoveSize    = 0x03;  // Mask

const Command  cmfFind        = 0x40;  // Main cmd
const Command  cmfFindNext    = 0x20;
const Command  cmfFindPrev    = 0x10;
const Command  cmfNotCharDn   = 0x02;
const Command  cmfNotCharUp   = 0x01;

const Command  cmgGoto        = 0x20;  // Main cmd
const Command  cmgGotoTop     = 0x08;  // Flag
const Command  cmgGotoBottom  = 0x04;  // Flag
const Command  cmgGotoForw    = 0x02;
const Command  cmgGotoBack    = 0x01;

const Command  cmNothing      = 0;
const Command  cmUseTop       = 1;
const Command  cmUseBottom    = 2;
const Command  cmNextDiff     = 3;
const Command  cmPrevDiff     = 4;
const Command  cmEditTop      = 5;
const Command  cmEditBottom   = 6;
const Command  cmSyncUp       = 7;
const Command  cmSyncDn       = 8;
const Command  cmShowRaster   = 9;
const Command  cmShowHelp     = 10;
const Command  cmSmartScroll  = 11;
const Command  cmQuit         = 12;

//--------------------------------------------------------------------
const int screenWidth = 140;  // Key value - but _must_ be constant!
//--------------------------------------------------------------------

const int screenHeight = 24;  // Enforced minimum height

const int  lineWidth = 32;    // Number of bytes displayed per line
const short leftMar  = 11;    // Starting column of hex display
const short leftMar2 = 108;   // Starting column of ASCII display

const int promptHeight = 3;   // Height of prompt window
const int inWidth = 12;       // Width of input window (excluding border)

const int searchIndent = lineWidth * 3;  // Lines of search result indentation

const int skipForw = 5;  // Percent to skip forward
const int skipBack = 1;  // Percent to skip backward

const int maxPath = 2000;

const VecSize maxHistory = 2000;

const char hexDigits[] = "0123456789ABCDEF%X";  // with Goto % and Goto hex

const char thouSep = '.';  // thousands separator (or '\0')

const File InvalidFile = -1;

int safeUC(int key);
char *pretty(char *buffer, FPos *size, int sign);
void showEditPrompt();
void showPrompt();
void exitMsg(int status, const char *message);

//--------------------------------------------------------------------
// Help panel text - max 21 lines (screenHeight - 3)

const char *aHelp[] = {
"  ",
"  ENTER   smartscroll",
"  PgDn    next different byte",
"  PgUp    prev different byte",
"  ",
"  Find Next Prev   search",
"  Goto             dec 0x x %",  /* longest */
"  + *     skip +5%",
"  -       skip -1%",
"  ",
"  E       edit file",
"  R       show raster",
"  Q Esc   quit",
"  ",
"      --- Two Files ---",
"  Enter     next diff",
"  # \\ =     prev diff",
"  1 2       sync views",
"  T         use only top",
"  B         use only bottom",
"  "
};

const int longestLine = 29;  // adjust!

const Byte aBold[] = { 6,3, 6,8, 6,13, 7,3, '\0' };  // hotkeys, with border
//--------------------------------------------------------------------

const int helpWidth = 1 + longestLine + 2 + 1;
const int helpHeight = 1 + sizeof(aHelp) / sizeof(aHelp[0]) + 1;

//====================================================================
// FileIO   :##io

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

inline Size ReadFile(File file, void* buffer, Size count)
{
  return read(file, buffer, count);
}

inline FPos SeekFile(File file, FPos position, int whence=SEEK_SET)
{
  return lseek(file, position, whence);
}

//====================================================================
// Class ConWindow   :##con

class ConWindow
{
 protected:
  PANEL   *pan;
  WINDOW  *win;

 public:
  ConWindow();
  ~ConWindow();
  void init(short x, short y, short width, short height, Style style);
  void close();
  void Border() { ::box(win, 0, 0); };
  void clear()  {   werase(win);    };
  void move(short x, short y) { move_panel(pan, y, x); };
  void put(short x, short y, const char* s) { mvwaddstr(win, y, x, s); };
  void putAttribs(short x, short y, Style color, short count);
  void putChar(short x, short y, char c, short count);
  int  readKey();
  void resize(short width, short height);
  void setAttribs(Style color);
  void setCursor(short x, short y);
  void update();
  void hide() { hide_panel(pan); };
  static void getScreenSize(int& x, int& y) { getmaxyx(curscr, y, x); };
  static void hideCursor()                  { curs_set(0);            };
  static void showCursor(bool insert=true)  { curs_set(insert ? 1 : 2); };
  static void shutdown();
  static bool startup();
}; // end ConWindow

bool ConWindow::startup()
{
  if (!initscr()) return false; // initialize the curses library
  set_escdelay(10);             // for static linking
  atexit(ConWindow::shutdown);  // just in case

  keypad(stdscr, true);         // enable keyboard mapping
  nonl();           // tell curses not to do NL->CR/NL on output
  cbreak();         // take input chars one at a time, no wait for \n
  noecho();         // do not echo input

  if (has_colors()) {
    start_color();

    init_pair(pairWhiteBlue,  COLOR_WHITE,  COLOR_BLUE);
    init_pair(pairWhiteBlack, COLOR_WHITE,  COLOR_BLACK);
    init_pair(pairRedWhite,   COLOR_RED,    COLOR_WHITE);
    init_pair(pairYellowBlue, COLOR_YELLOW, COLOR_BLUE);
    init_pair(pairGreenBlue,  COLOR_GREEN,  COLOR_BLUE);
    init_pair(pairBlackCyan,  COLOR_BLACK,  COLOR_CYAN);
    init_pair(pairGreenBlack, COLOR_GREEN,  COLOR_BLACK);
  } // end if terminal has color

  return true;
} // end ConWindow::startup

//--------------------------------------------------------------------
// Shut down the window system

void ConWindow::shutdown()
{
  if (!isendwin()) {
    showCursor();
    endwin();
  }
}

ConWindow::ConWindow()
: pan(NULL),
  win(NULL)
{
}

ConWindow::~ConWindow()
{
  close();
}

//--------------------------------------------------------------------
// Initialize the window

void ConWindow::init(short x, short y, short width, short height, Style attrib)
{
  if ((win = newwin(height, width, y, x)) == 0)
    exitMsg(99, "Internal error: Failed to create window");

  if ((pan = new_panel(win)) == 0)
    exitMsg(99, "Internal error: Failed to create panel");

  wbkgdset(win, attribStyle[attrib] | ' ');

  keypad(win, TRUE);            // enable keyboard mapping

  clear();
}

void ConWindow::close()
{
  if (pan) {
    del_panel(pan);
    pan = NULL;
  }

  if (win) {
    delwin(win);
    win = NULL;
  }
}

//--------------------------------------------------------------------
// Change the attributes of characters in the window

void ConWindow::putAttribs(short x, short y, Style color, short count)
{
  mvwchgat(win, y, x, count, attribStyle[color], colorStyle[color], NULL);
  touchwin(win);
}

//--------------------------------------------------------------------
// Write a character using the current attributes

void ConWindow::putChar(short x, short y, char c, short count)
{
  wmove(win, y, x);

  while (count--) {
    waddch(win, c);
  }
}

//--------------------------------------------------------------------
// Update the physical screen

void ConWindow::update()
{
  show_panel(pan);
  top_panel(pan);
  update_panels();
  doupdate();
}

//--------------------------------------------------------------------
// Read the next key down event

int ConWindow::readKey()
{
  update();

  return wgetch(win);
}

void ConWindow::resize(short width, short height)
{
  if (wresize(win, height, width) != OK)
    exitMsg(99, "Internal error: Failed to resize window");

  replace_panel(pan, win);

  clear();
}

void ConWindow::setAttribs(Style color)
{
  wattrset(win, attribStyle[color]);
}

//--------------------------------------------------------------------
// Position the cursor in the window

void ConWindow::setCursor(short x, short y)
{
  wmove(win, y, x);
}

//====================================================================
// Class Declarations

class Difference;

union FileBuffer
{
  Byte  line[1][lineWidth];
  Byte  buffer[lineWidth];
};

class FileDisplay  // :##file
{
 friend class Difference;

 protected:
  int                bufContents;
  FileBuffer*        data;
  const Difference*  diffs;
  FPos*              addr;
  File               file;
  char               fileName[maxPath];
  FPos               offset;
  FPos               prevOffset;
  ConWindow          win;
  bool               writable;

 public:
  FileDisplay();
  ~FileDisplay();
  void         init(int y, const Difference* aDiff);
  void         resize();
  void         shutDown();
  void         display();
  bool         edit(const FileDisplay* other);
  void         sync(const FileDisplay* other);
  void         move(FPos step) { moveTo(offset + step); };
  void         moveTo(FPos newOffset);
  void         moveToEnd();
  bool         moveTo(const Byte* searchFor, int searchLen);
  bool         moveToBack(const Byte* searchFor, int searchLen);
  bool         setFile(const char* aFileName);
  void         seekNotChar(bool upwards=false);
  void         skip(bool upwards=false);
  void         smartScroll();
  FPos         filesize;
  FPos         searchOff;
  FPos         scrollOff;
  int          se4rch;
  bool         advance;
  bool         editable;

 protected:
  void  setByte(short x, short y, Byte b);
}; // end FileDisplay

class Difference
{
 friend void FileDisplay::display();

 protected:
  FileBuffer*         data;
  const FileDisplay*  file1;
  const FileDisplay*  file2;

 public:
  Difference(const FileDisplay* aFile1, const FileDisplay* aFile2);
  ~Difference();
  int  compute();
  void resize();
}; // end Difference

class InputManager
{
 private:
  char*        buf;             // The editing buffer
  const char*  restrictChar;    // If non-NULL, only allow these chars
  StrVec&      history;         // The history vector to use
  StrMap       historyOverlay;  // Overlay of modified history entries
  VecSize      historyPos;      // The current offset into history[]
  int          maxLen;          // The size of buf (not including NUL)
  int          len;             // The current length of the string
  int          i;               // The current cursor position
  bool         upcase;          // Force all characters to uppercase?
  bool         splitHex;        // Entering space-separated hex bytes?
  bool         insert;          // False for overstrike mode

 public:
  InputManager(char* aBuf, int aMaxLen, StrVec& aHistory);
  bool run();
  void setCharacters(const char* aRestriction) { restrictChar = aRestriction; };
  void setSplitHex(bool val) { splitHex = val; };
  void setUpcase(bool val)   { upcase = val; };

 private:
  bool normalize(int pos);
  void useHistory(int delta);
}; // end InputManager

//====================================================================
// Global Variables   :##glob

String      lastSearch;
StrVec      hexSearchHistory, textSearchHistory, positionHistory;
ConWindow   promptWin, inWin, helpWin;
FileDisplay file1, file2;
Difference  diffs(&file1, &file2);
const char *program_name;  // Name under which this program was invoked
LockState   lockState;
bool        singleFile;
bool        showRaster;

int  linesTotal;    // Number of lines in curses
int  numLines;      // Number of lines of each file to display
int  linesBetween;  // Number of lines of padding between files
int  bufSize;       // Number of bytes of each file to display

// The number of bytes to move for each possible step size
//   See cmmMoveByte, cmmMoveLine, cmmMovePage
int  steps[4] = {1, lineWidth, bufSize-lineWidth, 0};

//====================================================================
// Class Difference

Difference::Difference(const FileDisplay* aFile1, const FileDisplay* aFile2)
: data(NULL),
  file1(aFile1),
  file2(aFile2)
{
}

Difference::~Difference()
{
  delete [] reinterpret_cast<Byte*>(data);
}

//--------------------------------------------------------------------
// Compute differences

int Difference::compute()
{
  if (singleFile)
    // We return 1 so that cmNextDiff won't keep searching
    return (file1->bufContents ? 1 : -1);

  memset(data->buffer, 0, bufSize); // Clear the difference table

  int  different = 0;

  const Byte*  buf1 = file1->data->buffer;
  const Byte*  buf2 = file2->data->buffer;

  int  size = min(file1->bufContents, file2->bufContents);

  int  i;
  for (i = 0; i < size; i++)
    if (*(buf1++) != *(buf2++)) {
      data->buffer[i] = true;
      ++different;
    }

  size = max(file1->bufContents, file2->bufContents);

  if (i < size) {
    // One buffer has more data than the other
    different += size - i;
    for (; i < size; i++)
      data->buffer[i] = true;   // These bytes are only in 1 buffer
  } else if (!size)
    return -1;                  // Both buffers are empty

  if (!different && (!file1->offset || !file2->offset))
    return 1;                   // File is at the beginning

  return different;
} // end Difference::compute

void Difference::resize()
{
  if (singleFile) return;

  if (data)
    delete [] reinterpret_cast<Byte*>(data);

  data = reinterpret_cast<FileBuffer*>(new Byte[bufSize]);
}

//====================================================================
// Class FileDisplay

FileDisplay::FileDisplay()
: bufContents(0),
  data(NULL),
  diffs(NULL),
  offset(0),
  prevOffset(0),
  writable(false),
  searchOff(0),
  scrollOff(0),
  se4rch(0),
  advance(false),
  editable(false)
{
  fileName[0] = '\0';
}

void FileDisplay::init(int y, const Difference* aDiff)
{
  diffs = aDiff;

  win.init(0, y, screenWidth, (numLines + 1 + (y ? 0 : linesBetween)), cFileWin);

  resize();
}

FileDisplay::~FileDisplay()
{
  shutDown();
  CloseFile(file);
  delete [] reinterpret_cast<Byte*>(data);
  free(addr);
}

void FileDisplay::resize()
{
  if (data) delete [] reinterpret_cast<Byte*>(data);
  data = reinterpret_cast<FileBuffer*>(new Byte[bufSize]);

  addr = (FPos*) calloc(numLines, sizeof(FPos));
}

//--------------------------------------------------------------------
// Shut down the file display

void FileDisplay::shutDown()
{
  win.close();
}

//--------------------------------------------------------------------
// Display the file contents   :##disp

void FileDisplay::display()
{
        if (! fileName[0]) return;

        short first, last, row, col, idx, lineLength;
        FPos lineOffset = offset;
        FPos diffOffset = offset - prevOffset;
        prevOffset = offset;
        Byte pos = (scrollOff ? scrollOff + lineWidth : offset + bufSize) * 100 / filesize;

        char bufStat[screenWidth + 1] = { 0 };
        memset(bufStat, ' ', screenWidth);

        char buf[90], buf2[2][40];
        sprintf(buf, " %s %s %d%%", pretty(buf2[0], &offset, 0), pretty(buf2[1], &diffOffset, 1), pos > 100 ? 100 : pos);

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

        win.put(0, 0, bufStat);
        win.putAttribs(0, 0, cFileName, strlen(bufStat));

        if (diffOffset < 0) {
                char *pc = (char*) memchr(buf, '-', strlen(buf));
                win.putAttribs(size_name + (pc - buf), 0, cFileSearch, 1);
        }

        char bufHex[screenWidth + 1] = { 0 };
        char bufAsc[  lineWidth + 1] = { 0 };

        for (row=0; row < numLines; ++row) {
                memset(bufHex, ' ', screenWidth);
                memset(bufAsc, ' ',   lineWidth);

                if (*(addr + row)) lineOffset += (lineWidth * (*(addr + row)));

                char *pbufHex = bufHex;
                pbufHex += sprintf(pbufHex, "%09lX ", lineOffset);

                lineLength = min(lineWidth, bufContents - row * lineWidth);

                for (col = idx = 0; col < lineLength; ++col, ++idx) {
                        if (! col) { *pbufHex++ = ' '; }

                        Byte b = data->line[row][col];

                        pbufHex += sprintf(pbufHex, "%02X ", b);

                        if      (b >= 0x20 && b <= 0x7E) { bufAsc[idx] = b; }

                        else if (b >= 0x09 && b <= 0x0D) { bufAsc[idx] = ' '; }

                        else                             { bufAsc[idx] = '.'; }
                }
                *pbufHex = ' ';

                win.put(0,        row + 1, bufHex);
                win.put(leftMar2, row + 1, bufAsc);

                for (col=0; col < 8; ++col) {
                        if (*(bufHex + col) != '0') { break; }
                }
                win.putAttribs(col, row + 1, cFileAddr, 9 - col);

                if (showRaster && bufHex[leftMar] != ' ')
                        for (col=0; col < 25; col += 8) {
                                win.putAttribs(leftMar  + col * 3 - 1, row + 1, cFileMark, 1);
                                win.putAttribs(leftMar2 + col        , row + 1, cFileMark, 1);
                        }

                if (diffs)
                        for (col=0; col < lineWidth; ++col)
                                if (diffs->data->line[row][col]) {
                                        win.putAttribs(leftMar  + col * 3, row + 1, cFileDiff, 2);
                                        win.putAttribs(leftMar2 + col    , row + 1, cFileDiff, 1);
                                }

                if (se4rch && row >= (searchOff ? searchIndent / lineWidth : 0))
                        for (col=0; col < lineWidth && se4rch; ++col, --se4rch) {
                                win.putAttribs(leftMar  + col * 3, row + 1, cFileSearch, 2);
                                win.putAttribs(leftMar2 + col    , row + 1, cFileSearch, 1);
                        }

                if (*(addr + row))
                        for (col=0; col < lineWidth; ++col) {
                                win.putAttribs(leftMar  + col * 3, row + 1, cFileDiff, 2);
                                win.putAttribs(leftMar2 + col    , row + 1, cFileDiff, 1);
                        }
                lineOffset += lineWidth;
        } // end for row up to numLines

        if (scrollOff) {
                moveTo(offset);  // reload buffer
                memset(addr, 0, numLines * sizeof(FPos));
        }
} // end FileDisplay::display

//--------------------------------------------------------------------
// Edit the file

bool FileDisplay::edit(const FileDisplay* other)
{
  if (!bufContents && offset)
    return false;               // You must not be completely past EOF

  file1.display();              // reset smartscroll

  if (!writable) {
    File w = OpenFile(fileName, true);
    if (w == InvalidFile) return false;
    CloseFile(file);
    file = w;
    writable = true;
  }

  if (bufContents < bufSize)
    memset(data->buffer + bufContents, 0, bufSize - bufContents);

  short x = 0;
  short y = 0;
  bool  hiNib = true;
  bool  ascii = false;
  bool  changed = false;
  int   key;

  showEditPrompt();
  win.setCursor(leftMar,1);
  ConWindow::showCursor();

  for (;;) {
    win.setCursor((ascii ? leftMar2 + x : leftMar + 3*x + !hiNib),
                  y+1);
    key = win.readKey();

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
         newByte = other->data->line[y][x]; // Copy from other file
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
             newByte = (newByte * 0x10) | (0x0F & data->line[y][x]);
           else
             newByte |= 0xF0 & data->line[y][x];
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
    promptWin.clear();
    promptWin.Border();
    promptWin.put(30,1,"Save changes (Y/N):");
    promptWin.setCursor(50,1);
    key = promptWin.readKey();
    if (safeUC(key) != 'Y') {
      changed = false;
      moveTo(offset);           // Re-read buffer contents
    } else {
      SeekFile(file, offset);
      WriteFile(file, data->buffer, bufContents);
    }
  }
  showPrompt();
  ConWindow::hideCursor();
  return changed;
} // end FileDisplay::edit

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
    data->line[y][x] = b ^ 1;         // Make sure it's different
  } // end if past the end

  if (data->line[y][x] != b) {
    data->line[y][x] = b;
    char str[3];
    sprintf(str, "%02X", b);
    win.setAttribs(cFileEdit);
    win.put(leftMar + 3*x, y+1, str);
    str[0] = b >= 0x20 && b <= 0x7E ? b : '.';
    str[1] = '\0';
    win.put(leftMar2 + x, y+1, str);
    win.setAttribs(cFileWin);
  }
} // end FileDisplay::setByte

//--------------------------------------------------------------------
// Change the file position   :##move

void FileDisplay::moveTo(FPos newOffset)
{
        if (! fileName[0]) return;

        offset = newOffset;

        if (offset < 0) {
                offset = 0;
        }
        if (offset > filesize) {
                offset = filesize;
        }

        SeekFile(file, offset);

        bufContents = ReadFile(file, data->buffer, bufSize);
}

//--------------------------------------------------------------------
// Change the file position by searching

bool FileDisplay::moveTo(const Byte* searchFor, int searchLen)
{
        if (! fileName[0]) return true; // No file, pretend success

        const int blockSize = 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize + searchLen];

        FPos newPos = searchOff ? searchOff : offset;
        if (advance) ++newPos;
        SeekFile(file, newPos);
        Size bytesRead = ReadFile(file, searchBuf + searchLen, blockSize);

        for (int l = searchLen; bytesRead > 0; l = 0) { // adjust for first block
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
                                        return true;
                                }
                        }
                }
                newPos += blockSize;
                memcpy(searchBuf, searchBuf + blockSize, searchLen);
                bytesRead = ReadFile(file, searchBuf + searchLen, blockSize);
        }
        advance = false;
        delete [] searchBuf;
        moveToEnd();
        return false;
} // end FileDisplay::moveTo

//--------------------------------------------------------------------
// Change the file position by searching backward

bool FileDisplay::moveToBack(const Byte* searchFor, int searchLen)
{
        if (! fileName[0]) return true;

        const int blockSize = 8 * 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize + searchLen];
        FPos newPos = (searchOff ? searchOff : offset) - blockSize;
        memcpy(searchBuf + blockSize, data->buffer + (searchOff ? searchIndent : 0), searchLen);
        int diff = 0;

        for (int l=advance ? 0 : 1; ; l=0) {
                if (newPos < 0) { diff = newPos; newPos = 0; }
                SeekFile(file, newPos);
                if ((ReadFile(file, searchBuf, blockSize)) <= 0) break;

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
                                        return true;
                                }
                        }
                }
                if (! newPos) break;

                memcpy(searchBuf + blockSize, searchBuf, searchLen);
                newPos -= blockSize;
        }
        advance = false;
        delete [] searchBuf;
        moveTo(0);
        return false;
} // end FileDisplay::moveToBack

//--------------------------------------------------------------------
// Move to the end of the file

void FileDisplay::moveToEnd()
{
        if (! fileName[0]) return;

        FPos end = filesize - steps[cmmMovePage];

        moveTo(end);
}

//--------------------------------------------------------------------
// Seek to next byte not equal to current head

void FileDisplay::seekNotChar(bool upwards)
{
        if (! fileName[0]) return;

        const int blockSize = 1024 * 1024;
        Byte *const searchBuf = new Byte[blockSize];
        const Byte searchFor = *data->buffer;
        FPos newPos = upwards ? offset - blockSize : offset + 1;
        Size bytesRead;
        int diff = 0;

        for (;;) {
                if (newPos < 0) { diff = newPos; newPos = 0; }
                SeekFile(file, newPos);
                if ((bytesRead = ReadFile(file, searchBuf, blockSize)) <= 0) break;

                if (upwards) {
                        for (int i = blockSize - 1 + diff; i >= 0; --i) {
                                if (searchBuf[i] != searchFor) {
                                        delete [] searchBuf;
                                        if (newPos + i >= steps[cmmMovePage])
                                                moveTo(newPos + i - steps[cmmMovePage]);
                                        else moveTo(newPos + i);
                                        se4rch = 1; return;
                                }
                        }
                        if (! newPos) break;
                        newPos -= blockSize;
                }
                else {
                        for (int i=0; i < bytesRead; ++i) {
                                if (searchBuf[i] != searchFor) {
                                        delete [] searchBuf;
                                        moveTo(newPos + i);
                                        se4rch = 1; return;
                                }
                        }
                        newPos += blockSize;
                }
        }
        delete [] searchBuf;
        if (upwards) { moveTo(0); }
        else { moveToEnd(); }
        return;
} // end FileDisplay::seekNotChar

//--------------------------------------------------------------------
// Jump a specific percentage forward / backward

void FileDisplay::skip(bool upwards)
{
        if (! fileName[0]) return;

        FPos step = filesize / 100;

        if (upwards)
                move(step * -skipBack);
        else
                move(step * skipForw);
}

//--------------------------------------------------------------------
// Scroll forward with skipping same content lines

void FileDisplay::smartScroll()
{
        FPos newPos = scrollOff ? scrollOff : (offset + 0x10) & ~0xF;

        if (filesize - newPos < bufSize) {
                scrollOff = 0;
                moveTo(newPos);
                return;
        }
        offset = newPos;

        const int blockSize = 1000 * bufSize;
        FileBuffer* scrollBuf = reinterpret_cast<FileBuffer*>(new Byte[blockSize]);

        Byte buf[lineWidth];
        uint repeat = 0;

        SeekFile(file, newPos);
        Size bytesRead = ReadFile(file, scrollBuf->buffer, blockSize);

        memcpy(data->buffer, scrollBuf->buffer, lineWidth);
        bytesRead -= lineWidth;

        int i = 1, j = 1;
        for ( ; bytesRead > 0; ) {
                if (bytesRead >= lineWidth) {
                        memcpy(buf, scrollBuf->line[j], lineWidth);

                        if (memcmp(data->line[i - 1], buf, lineWidth)) {
                                memcpy(data->line[i], buf, lineWidth);

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

                        SeekFile(file, newPos);
                        bytesRead = ReadFile(file, scrollBuf->buffer, blockSize);

                        if (bytesRead > 0) {
                                if (bytesRead < lineWidth) {
                                        *(addr + i) = repeat;

                                        memcpy(data->line[i], scrollBuf->line[j], bytesRead);
                                        break;
                                }

                        } else {
                                if (repeat) {
                                        *(addr + i) = --repeat;

                                        memcpy(data->line[i], data->line[i - 1], lineWidth);
                                        ++i;
                                }
                        }
                }
        }

        scrollOff = newPos + j * lineWidth;

        bufContents = i * lineWidth + min(lineWidth, (int) bytesRead);

        delete [] reinterpret_cast<Byte*>(scrollBuf);
        return;
} // end FileDisplay::smartScroll

//--------------------------------------------------------------------
// Synchronize the plains
//
// '1' | upper:  sync file1 with file2
// '2' | lower:  sync file2 with file1

void FileDisplay::sync(const FileDisplay* other)
{
        if (singleFile) return;

        if (other->bufContents) {
                moveTo(other->offset);
        }
        else {
                moveToEnd();
        }
}

//--------------------------------------------------------------------
// Open a file for display

bool FileDisplay::setFile(const char* aFileName)
{
        strncpy(fileName, aFileName, maxPath);
        fileName[maxPath - 1] = '\0';

        File e = OpenFile(fileName, true);
        if (e > InvalidFile) {
                editable = true;
                CloseFile(e);
        }

        bufContents = 0;
        file = OpenFile(fileName);
        writable = false;

        if (file == InvalidFile)
                return false;

        filesize = SeekFile(file, 0, SEEK_END);
        SeekFile(file, 0);
        offset = 0;
        bufContents = ReadFile(file, data->buffer, bufSize);

        return true;
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
// Construct the InputManager object

InputManager::InputManager(char* aBuf, int aMaxLen, StrVec& aHistory)
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
// Run the main loop to get an input string

bool InputManager::run()
{
  inWin.setCursor(2,1);

  bool  done   = false;
  bool  aborted = true;

  ConWindow::showCursor(insert);

  memset(buf, ' ', maxLen);
  buf[maxLen] = '\0';

  // We need to be able to display complete bytes:
  if (splitHex && (maxLen % 3 == 1)) --maxLen;

  // Main input loop:
  while (!done) {
    inWin.put(2,1,buf);
    inWin.setCursor(2+i,1);
    int key = inWin.readKey();
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

     case 0x0B:                 // Ctrl-K
      if (len > i) {
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
      if (historyPos == 0) beep();
      else                 useHistory(-1);
      break;

     case 0x0E:                 // Ctrl-N
     case KEY_DOWN:
      if (historyPos == history.size()) beep();
      else                              useHistory(+1);
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
        buf[i++] = key;
        if (splitHex && (i < maxLen) && (i % 3 == 2))
          ++i;
        if (i > len) len = i;
      } // end if is acceptable character to insert
    } // end switch key
  } // end while not done

  // Hide the input window & cursor:
  ConWindow::hideCursor();
  inWin.hide();

  // Record the result in the history:
  if (!aborted && len) {
    String  newValue(buf);

    SVItr  exists = find(history.begin(), history.end(), newValue);
    if (exists != history.end())
      // Already in history.  Move it to the end:
      rotate(exists, exists + 1, history.end());
    else if (history.size() >= maxHistory) {
      // History is full.  Replace the first entry & move it to the end:
      history.front().swap(newValue);
      rotate(history.begin(), history.begin() + 1, history.end());
    } else
      // Just append to history:
      history.push_back(newValue);
  } // end if we have a value to store in the history

  return !aborted;
} // end run

//--------------------------------------------------------------------
// Switch the current input line with one from the history
//
// Input:
//   delta:  The number to add to historyPos (-1 previous, +1 next)

void InputManager::useHistory(int delta)
{
  // Clean up the current string if necessary:
  normalize(i);

  // Update the history overlay if necessary:
  //   We always store the initial value, because it doesn't
  //   correspond to a valid entry in history.
  if (len || historyPos == history.size())
    historyOverlay[historyPos].assign(buf, len);

  // Look for an entry in the overlay:
  SMItr itr = historyOverlay.find(historyPos += delta);

  String& s = ((itr == historyOverlay.end())
               ? history[historyPos] : itr->second);

  // Store the new string in the buffer:
  memset(buf, ' ', maxLen);
  i = len = min(static_cast<VecSize>(maxLen), s.length());
  memcpy(buf, s.c_str(), len);
} // end useHistory

//====================================================================
// Global Functions   :##gf

void calcScreenLayout(bool resize = true)
{
  int  screenX, screenY;

  ConWindow::getScreenSize(screenX, screenY);

  if (screenX < screenWidth) {
    ostringstream  err;
    err << "The screen must be at least "
        << screenWidth << " characters wide.";
    exitMsg(2, err.str().c_str());
  }

  if (screenY < screenHeight) {
    ostringstream  err;
    err << "The screen must be at least "
        << screenHeight << " lines high.";
    exitMsg(2, err.str().c_str());
  }

  numLines = screenY - promptHeight - (singleFile ? 1 : 2);
  linesTotal = screenY;

  if (singleFile)
    linesBetween = 0;
  else {
    linesBetween = numLines % 2;
    numLines = (numLines - linesBetween) / 2;
  }

  bufSize = numLines * lineWidth;

  steps[cmmMovePage] = bufSize - lineWidth;
} // end calcScreenLayout

//--------------------------------------------------------------------
// Convert a character to uppercase

int safeUC(int c)
{
  return (c >= 0 && c <= UCHAR_MAX) ? toupper(c) : c;
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

inline const char* ErrorMsg()
{
  return strerror(errno);
}

//--------------------------------------------------------------------
// Get a string using inWin

void getString(char* buf, int maxLen, StrVec& history,
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
// (Hot)keys in promptWin   :##hkey

void displayLockState()
{
        promptWin.putAttribs(49,  1, cHotKey, 1);  // Find
        promptWin.putAttribs(55,  1, cHotKey, 1);  // Next
        promptWin.putAttribs(60,  1, cHotKey, 1);  // Prev

        promptWin.putAttribs(67,  1, cHotKey, 1);  // Goto
        promptWin.putAttribs(99,  1, cHotKey, 1);  // Raster

        promptWin.putAttribs(108, 1, (file1.editable ? cHotKey : cBackground), 1);  // Edit

        if (singleFile) return;

        if (lockState == lockTop) {  // edit bottom
                promptWin.putAttribs(108, 1, (file2.editable ? cHotKey : cBackground), 1);
        }

        if (lockState == lockBottom) {
                promptWin.putAttribs(124, 1, cCurrentMode, 3);
                promptWin.putAttribs(128, 1, cBackground, 6);
                promptWin.putAttribs(128, 1, cHotKey, 1);
        }
        else if (lockState == lockTop) {
                promptWin.putAttribs(124, 1, cBackground, 3);
                promptWin.putAttribs(124, 1, cHotKey, 1);
                promptWin.putAttribs(128, 1, cCurrentMode, 6);
        }
        else {
                promptWin.putAttribs(124, 1, cBackground, 3);
                promptWin.putAttribs(124, 1, cHotKey, 1);
                promptWin.putAttribs(128, 1, cBackground, 6);
                promptWin.putAttribs(128, 1, cHotKey, 1);
        }
}

//--------------------------------------------------------------------
// Feedback for (possibly) long-running commands

void displaySearch(Byte cmd)
{
        if (! cmd) { usleep(150000); }

        promptWin.putAttribs(35, 1, cBackground, 5);  // ENTER (smartscroll)

        promptWin.putAttribs(55, 1, cBackground, 9);  // Find Next + Prev
        promptWin.putAttribs(81, 1, cBackground, 9);  // Not Char Down + Up
        promptWin.putAttribs(55, 1, cHotKey, 1);
        promptWin.putAttribs(60, 1, cHotKey, 1);

        if      (cmd == cmSmartScroll) { promptWin.putAttribs(35, 1, cCurrentMode, 5); }

        else if (cmd & cmfFindNext)    { promptWin.putAttribs(55, 1, cCurrentMode, 4); }
        else if (cmd & cmfFindPrev)    { promptWin.putAttribs(60, 1, cCurrentMode, 4); }
        else if (cmd & cmfNotCharDn)   { promptWin.putAttribs(81, 1, cCurrentMode, 4); }
        else if (cmd & cmfNotCharUp)   { promptWin.putAttribs(86, 1, cCurrentMode, 4); }

        promptWin.update();
}

//--------------------------------------------------------------------
// Move help window in stack

void displayHelp()
{
        helpWin.update();
        getch();
        helpWin.hide();
}

//--------------------------------------------------------------------
// Print a message to stderr and exit

void exitMsg(int status, const char* message)
{
  ConWindow::shutdown();

  cerr << endl << message << endl;
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
  inWin.resize(width, 3);
  inWin.move((screenWidth-width)/2,
             ((!singleFile && (cmd & cmgGotoBottom))
              ? ((cmd & cmgGotoTop)
                 ? numLines + linesBetween                   // Moving both
                 : numLines + numLines/2 + 1 + linesBetween) // Moving bottom
              : numLines/2));                                // Moving top

  inWin.Border();
  inWin.put((width-strlen(title))/2,0, title);
}

//--------------------------------------------------------------------
// Display prompt window for editing

void showEditPrompt()
{
        promptWin.clear();
        promptWin.Border();

        promptWin.put(3, 1, "Arrow keys move cursor        TAB hex<>ASCII       ESC done");

        promptWin.putAttribs( 3, 1, cPromptKey, 10);
        promptWin.putAttribs(33, 1, cPromptKey, 3);
        promptWin.putAttribs(54, 1, cPromptKey, 3);

        if (! singleFile) {
                promptWin.put(71, 1, "RET copy byte from other file");
                promptWin.putAttribs(71, 1, cPromptKey, 3);
        }
}

//--------------------------------------------------------------------
// Display prompt window   :##show

void showPrompt()
{
        promptWin.clear();
        promptWin.Border();

        promptWin.put(1, 1,
  " arrows  home end  space bspace   enter #|\\|=   Find  Next Prev   Goto"
// 12345678_112345678_212345678_312345678_412 345678_512345678_612345678_7
  "  +|* -   pgdn pgup   1 2   Raster   Edit   esc|Q    Top Bottom");
// 12345678_812345678_912345678_012345678_112345678_212345678_312345678_4;

        if (singleFile) {
                promptWin.put(35,  1, "ENTER");

                promptWin.putChar(41,  1, ' ', 5);   // #
                promptWin.putChar(93,  1, ' ', 3);   // 1
                promptWin.putChar(124, 1, ' ', 10);  // Top
        }

        displayLockState();
}

//--------------------------------------------------------------------
// Initialize program

bool initialize()
{
        if (! ConWindow::startup())  // curses init
                return false;

        ConWindow::hideCursor();

        calcScreenLayout(false);  // global vars

        inWin.init(0, 0, inWidth + 2, 3, cPromptBdr);
        inWin.Border();
        inWin.put((inWidth - 4) / 2, 0, " Goto ");
        inWin.setAttribs(cPromptWin);
        inWin.hide();

        helpWin.init(1 + (screenWidth - helpWidth) / 2, 1 + (linesTotal - helpHeight) / 3, helpWidth, helpHeight, cPromptBdr);
        helpWin.Border();

        helpWin.put((helpWidth - 6) / 2, 0, " Help ");
        helpWin.put((helpWidth - 20 - 3 - 1) / 2, helpHeight - 1, " VBinDiff for Linux " VBL_VERSION " ");

        for (size_t i=0; i < helpHeight - 1 - 1; ++i) {  // exclude border
                helpWin.put(1, i + 1, aHelp[i]);
        }

        for (int i=0; aBold[i]; i += 2) {
                helpWin.putAttribs(aBold[i + 1], aBold[i], cHotKey, 1);
        }

        helpWin.setAttribs(cPromptWin);
        helpWin.hide();

        int y;
        if (singleFile) {
                y = numLines + 1;
        }
        else {
                y = numLines * 2 + linesBetween + 2;
        }

        promptWin.init(0, y, screenWidth, promptHeight, cBackground);

        if (! singleFile)
                diffs.resize();

        file1.init(0, (singleFile ? NULL : &diffs));

        if (! singleFile)
                file2.init(numLines + linesBetween + 1, &diffs);

        return true;
} // end initialize

//--------------------------------------------------------------------
// Get a command from the keyboard   :##get

Command getCommand()
{
        Command cmd = cmNothing;

        while (cmd == cmNothing) {
                int e = promptWin.readKey();

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
                        case '*':  cmd = cmgGoto | cmgGotoForw; break;
                        case '-':  cmd = cmgGoto | cmgGotoBack; break;

                        case 'T':  if (! singleFile) cmd = cmUseTop; break;
                        case 'B':  if (! singleFile) cmd = cmUseBottom; break;

                        case KEY_RETURN:
                                if (singleFile)
                                        cmd = cmSmartScroll;
                                else
                                        cmd = cmNextDiff;
                                break;

                        case '#':
                        case '\\':
                        case '=':  cmd = cmPrevDiff; break;

                        case 'E':
                                if (lockState == lockTop)
                                        cmd = cmEditBottom;
                                else
                                        cmd = cmEditTop;
                                break;

                        case '1':  cmd = cmSyncUp; break;
                        case '2':  cmd = cmSyncDn; break;

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

                if (lockState != lockBottom)
                        cmd |= cmgGotoBottom;
        }

        return cmd;
} // end getCommand

//--------------------------------------------------------------------
// Get a file position and move there

void gotoPosition(Command cmd)
{
        positionInWin(cmd, inWidth + 4, " Goto ");

        const int maxLen = inWidth - 1;
        char buf[maxLen + 1];

        getString(buf, maxLen, positionHistory, hexDigits, true);
        if (! buf[0]) return;

        FPos pos1 = 0, pos2 = 0;

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
                file1.moveTo(pos1);
        }
        if (cmd & cmgGotoBottom) {
                file2.moveTo(pos2);
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

                inWin.put(2,  1, "H Hex");
                inWin.put(10, 1, "T Text");
                inWin.putAttribs(2,  1, cHotKey, 1);
                inWin.putAttribs(10, 1, cHotKey, 1);

                if (havePrev) {
                        inWin.put(19, 1, "N Next");
                        inWin.put(28, 1, "P Prev");
                        inWin.putAttribs(19, 1, cHotKey, 1);
                        inWin.putAttribs(28, 1, cHotKey, 1);
                }
                key = safeUC(inWin.readKey());

                bool hex = false;

                if (key == KEY_ESCAPE) {
                        inWin.hide(); return;
                }
                else if (key == 'H') {
                        hex = true;
                }

                if ((key == 'N' || key == 'P') && havePrev) {
                        inWin.hide();
                }
                else {
                        positionInWin(cmd, screenWidth, (hex ? " Find Hex Bytes" : " Find Text "));

                        const int maxLen = screenWidth - 4;
                        Byte buf[maxLen + 1];
                        int searchLen;

                        if (hex) {
                                getString(reinterpret_cast<char*>(buf), maxLen, hexSearchHistory, hexDigits, true, true);

                                searchLen = packHex(buf);
                        }
                        else {
                                getString(reinterpret_cast<char*>(buf), maxLen, textSearchHistory);

                                searchLen = strlen(reinterpret_cast<char*>(buf));
                        }

                        if (! searchLen) return;

                        lastSearch.assign(reinterpret_cast<char*>(buf), searchLen);

                        file1.advance = file2.advance = false;
                }
        } // end direct N or P

        const Byte *const searchPattern = reinterpret_cast<const Byte*>(lastSearch.c_str());

        if (cmd & cmfFindPrev || key == 'P') {
                displaySearch(cmfFindPrev);

                if (cmd & cmgGotoTop) {
                        file1.moveToBack(searchPattern, lastSearch.length());
                }
                if (cmd & cmgGotoBottom) {
                        file2.moveToBack(searchPattern, lastSearch.length());
                }
        }
        else {
                displaySearch(cmfFindNext);

                if (cmd & cmgGotoTop) {
                        file1.moveTo(searchPattern, lastSearch.length());
                }
                if (cmd & cmgGotoBottom) {
                        file2.moveTo(searchPattern, lastSearch.length());
                }
        }
} // end searchFiles

//--------------------------------------------------------------------
// Handle a command   :##hand

void handleCmd(Command cmd)
{
        if (cmd & cmmMove) {
                int step = steps[cmd & cmmMoveSize];

                if (! (cmd & cmmMoveForward)) {
                        step *= -1;
                }

                if ((cmd & cmmMoveForward) && ! step) {  // special case first
                        if (cmd & cmgGotoTop) {
                                file1.moveToEnd();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.moveToEnd();
                        }
                }
                else {
                        if (cmd & cmgGotoTop) {
                                if (step) {
                                        file1.move(step);
                                }
                                else {
                                        file1.moveTo(0);
                                }
                        }
                        if (cmd & cmgGotoBottom) {
                                if (step) {
                                        file2.move(step);
                                }
                                else {
                                        file2.moveTo(0);
                                }
                        }
                }
        }

        else if (cmd & cmfFind) {
                if (cmd & cmfNotCharDn) {
                        displaySearch(cmd);

                        if (cmd & cmgGotoTop) {
                                file1.seekNotChar();
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.seekNotChar();
                        }
                }
                else if (cmd & cmfNotCharUp) {
                        displaySearch(cmd);

                        if (cmd & cmgGotoTop) {
                                file1.seekNotChar(true);
                        }
                        if (cmd & cmgGotoBottom) {
                                file2.seekNotChar(true);
                        }
                }
                else {
                        searchFiles(cmd);
                }
                displaySearch(0);
        }

        else if (cmd & cmgGoto) {
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
                else {
                        gotoPosition(cmd);
                }
        }

        else if (cmd == cmSyncUp) {
                file1.sync(&file2);
        }
        else if (cmd == cmSyncDn) {
                file2.sync(&file1);
        }

        else if (cmd == cmNextDiff) {
                if (lockState) {
                        lockState = lockNeither;
                        displayLockState();
                }

                do {
                        file1.move(bufSize);
                        file2.move(bufSize);
                } while (! diffs.compute());
        }

        else if (cmd == cmPrevDiff) {
                if (lockState) {
                        lockState = lockNeither;
                        displayLockState();
                }

                do {
                        file1.move(-bufSize);
                        file2.move(-bufSize);
                } while (! diffs.compute());
        }

        else if (cmd == cmUseTop) {
                if (lockState == lockBottom) {
                        lockState = lockNeither;
                }
                else {
                        lockState = lockBottom;
                }
                displayLockState();
        }

        else if (cmd == cmUseBottom) {
                if (lockState == lockTop)
                        lockState = lockNeither;
                else {
                        lockState = lockTop;
                }
                displayLockState();
        }

        else if (cmd == cmShowRaster) {
                showRaster ^= true;
        }

        else if (cmd == cmShowHelp) {
                displayHelp();
        }

        else if (cmd == cmEditTop) {
                file1.edit(singleFile ? NULL : &file2);
        }

        else if (cmd == cmEditBottom) {
                file2.edit(&file1);
        }

        else if (cmd == cmSmartScroll) {
                displaySearch(cmd);

                file1.smartScroll();
                displaySearch(0);
        }

        while (diffs.compute() < 0) {
                file1.move(-steps[cmmMovePage]);
                file2.move(-steps[cmmMovePage]);
        }

        file1.display();
        file2.display();
} // end handleCmd

//====================================================================
// Main Program   :##main

int main(int argc, char* argv[])
{
        if ((program_name = strrchr(argv[0], '/')))
                ++program_name;
        else
                program_name = argv[0];

        cout << "VBinDiff for Linux " << VBL_VERSION << endl;

        if (argc < 2 || argc > 3) {
                cout << "\n"
                        "\t" << program_name << " file1 [file2]\n"
                        "\n"
                        "// type 'h' for help\n"
                        << endl;
                exit(0);
        }
        singleFile = (argc == 2);

        if (! initialize()) {
                cerr << endl << program_name << ": Unable to initialize windows" << endl;
                return 1;
        }

        {
                ostringstream errMsg;

                if (! file1.setFile(argv[1])) {
                        const char* errStr = ErrorMsg();
                        errMsg << "Unable to open " << argv[1] << ": " << errStr;
                }
                else if (! singleFile && ! file2.setFile(argv[2])) {
                        const char* errStr = ErrorMsg();
                        errMsg << "Unable to open " << argv[2] << ": " << errStr;
                }
                else if (! file1.filesize) {
                        errMsg << "File is empty: " << argv[1];
                }
                else if (! singleFile && ! file2.filesize) {
                        errMsg << "File is empty: " << argv[2];
                }
                else if (file1.filesize > 68719476735) {
                        errMsg << "File is too big: " << argv[1];
                }
                else if (! singleFile && file2.filesize > 68719476735) {
                        errMsg << "File is too big: " << argv[2];
                }
                string error(errMsg.str());

                if (error.length())
                        exitMsg(1, error.c_str());
        }

        diffs.compute();

        showPrompt();
        file1.display();
        file2.display();

        Command  cmd;
        while ((cmd = getCommand()) != cmQuit) {
                if (! (cmd & cmfFind && ! (cmd & (cmfNotCharDn | cmfNotCharUp)))) {
                        file1.searchOff = file2.searchOff = 0;
                        file1.advance = file2.advance = false;
                }
                if (cmd != cmSmartScroll) {
                        file1.scrollOff = 0;
                }
                handleCmd(cmd);
        }

        file1.shutDown();
        file2.shutDown();

        helpWin.close();
        inWin.close();
        promptWin.close();

        ConWindow::shutdown();

        return 0;
} // end main

