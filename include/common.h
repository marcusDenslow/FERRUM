
#ifndef COMMON_H
#define COMMON_H

// Standard library includes
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>    // For isprint
#include <fcntl.h>    // For file control
#include <unistd.h>   // For POSIX API (replaces direct.h)
#include <sys/types.h>
#include <sys/wait.h>  // For waitpid
#include <sys/stat.h>  // For stat (replaces fileapi.h)
#include <termios.h>   // For terminal control
#include <sys/ioctl.h> // For console dimensions
#include <signal.h>    // For signal handling
#include <time.h>      // For time functions
#include <limits.h>    // For PATH_MAX

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Common defines
#define LSH_RL_BUFSIZE 1024
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"

// Terminal control and color codes (ANSI sequences)
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_BLACK   "\x1b[30m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[37m"

// Background colors
#define ANSI_BG_BLACK   "\x1b[40m"
#define ANSI_BG_RED     "\x1b[41m"
#define ANSI_BG_GREEN   "\x1b[42m"
#define ANSI_BG_YELLOW  "\x1b[43m"
#define ANSI_BG_BLUE    "\x1b[44m"
#define ANSI_BG_MAGENTA "\x1b[45m"
#define ANSI_BG_CYAN    "\x1b[46m"
#define ANSI_BG_WHITE   "\x1b[47m"

// Cursor movement and screen control
#define ANSI_CLEAR_SCREEN "\x1b[2J"
#define ANSI_CURSOR_HOME  "\x1b[H"
#define ANSI_SAVE_CURSOR  "\x1b[s"
#define ANSI_RESTORE_CURSOR "\x1b[u"

// Key code definitions used by the line reader
#define KEY_BACKSPACE 127  // Different on Linux
#define KEY_TAB 9
#define KEY_ENTER 10       // Linux: 10 (LF) or 13 (CR)
#define KEY_ESCAPE 27

// Special keys (Linux terminal uses escape sequences)
// These will need handling in the line reader
#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_LEFT  1002
#define KEY_RIGHT 1003
#define KEY_SHIFT_ENTER 1010
#define KEY_SHIFT_TAB 1011

// Typedefs for compatibility
typedef unsigned int UINT;
typedef unsigned short WORD;
typedef int BOOL;
typedef int HANDLE;
typedef int DWORD;

// Define Windows constants
#define FALSE 0
#define TRUE 1

// Struct to replace COORD
typedef struct {
    short X;
    short Y;
} COORD;

// Struct to replace SMALL_RECT
typedef struct {
    short Left;
    short Top;
    short Right;
    short Bottom;
} SMALL_RECT;

// Struct to replace CONSOLE_SCREEN_BUFFER_INFO
typedef struct {
    COORD dwSize;
    COORD dwCursorPosition;
    WORD wAttributes;
    SMALL_RECT srWindow;
} CONSOLE_SCREEN_BUFFER_INFO;

// Struct to replace CONSOLE_CURSOR_INFO
typedef struct {
    DWORD dwSize;
    BOOL bVisible;
} CONSOLE_CURSOR_INFO;

#endif // COMMON_H
