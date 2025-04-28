#ifndef THEMES_H
#define THEMES_H

#include "common.h"

typedef struct {
  // Standard console colors
  WORD PRIMARY_COLOR;   // Main text color
  WORD SECONDARY_COLOR; // Secondary text color
  WORD ACCENT_COLOR;    // Color for highlights/important elements
  WORD SUCCESS_COLOR;   // Success messages
  WORD ERROR_COLOR;     // Error messages
  WORD WARNING_COLOR;   // Warning messages

  WORD INVALID_COMMAND_COLOR; // unrecognized command

  // UI element colors
  WORD HEADER_COLOR;      // Headers and titles
  WORD STATUS_BAR_COLOR;  // Status bar background
  WORD STATUS_TEXT_COLOR; // Status bar text
  WORD PROMPT_COLOR;      // Command prompt

  // File system colors
  WORD DIRECTORY_COLOR;    // Directory names
  WORD EXECUTABLE_COLOR;   // Executable files
  WORD TEXT_FILE_COLOR;    // Text files
  WORD IMAGE_FILE_COLOR;   // Image files
  WORD CODE_FILE_COLOR;    // Code files (non-executable)
  WORD ARCHIVE_FILE_COLOR; // Archive files

  // Syntax highlighting colors
  WORD SYNTAX_KEYWORD;      // Keywords in syntax highlighting
  WORD SYNTAX_STRING;       // Strings in syntax highlighting
  WORD SYNTAX_COMMENT;      // Comments in syntax highlighting
  WORD SYNTAX_NUMBER;       // Numbers in syntax highlighting
  WORD SYNTAX_PREPROCESSOR; // Preprocessor in syntax highlighting

  // ANSI true color definitions (for modern terminals)
  char ANSI_BASE[20];      // Base color
  char ANSI_SURFACE[20];   // Surface color
  char ANSI_OVERLAY[20];   // Overlay color
  char ANSI_MUTED[20];     // Muted color
  char ANSI_SUBTLE[20];    // Subtle color
  char ANSI_TEXT[20];      // Text color
  char ANSI_LOVE[20];      // Love/Red color
  char ANSI_GOLD[20];      // Gold color
  char ANSI_ROSE[20];      // Rose/pink color
  char ANSI_PINE[20];      // Pine color
  char ANSI_FOAM[20];      // Foam color
  char ANSI_IRIS[20];      // Iris color
  char ANSI_HIGHLIGHT[20]; // Highlight color

  char ANSI_INVALID_COMMAND[20];

  // Flag to indicate if ANSI colors should be used
  BOOL use_ansi_colors;

  // Name of the theme
  char name[32];
} ShellTheme;

// Initialize theme system
void init_theme_system(void);

// Shutdown theme system
void shutdown_themes(void);

// Initialize themes for shell startup
void init_themes(void);

// Load a theme by name
int load_theme(const char *theme_name);

// Apply current theme settings
void apply_current_theme(void);

// Get current theme
const ShellTheme *get_current_theme(void);

// List available themes
void list_available_themes(void);

// Get theme names for tab completion
char **get_theme_names(int *theme_count);

// Command handler for "theme" command
int lsh_theme(char **args);

// Current active theme
extern ShellTheme current_theme;

#endif // THEMES_H