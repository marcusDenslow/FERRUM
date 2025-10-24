
#include "themes.h"
#include <stdio.h>
#include <string.h>

// Define Windows colors for compatibility
#define FOREGROUND_BLUE      0x0001
#define FOREGROUND_GREEN     0x0002
#define FOREGROUND_RED       0x0004
#define FOREGROUND_INTENSITY 0x0008

#define FOREGROUND_CYAN (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)

// Current active theme
ShellTheme current_theme;

// Define built-in themes
static ShellTheme default_theme = {
    // Standard console colors
    .PRIMARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,  // White (dimmer)
    .ACCENT_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY,  // Bright Green
    .SUCCESS_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,     // Bright Red
    .WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                     FOREGROUND_INTENSITY, // Bright Yellow

    .HEADER_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .STATUS_BAR_COLOR = 0,                                   // Black
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY,              // Bright White
    .PROMPT_COLOR = FOREGROUND_CYAN | FOREGROUND_INTENSITY, // Bright Cyan

    .DIRECTORY_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                        FOREGROUND_INTENSITY, // Bright Yellow
    .TEXT_FILE_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White
    .IMAGE_FILE_COLOR = FOREGROUND_RED | FOREGROUND_BLUE |
                        FOREGROUND_INTENSITY,                  // Bright Magenta
    .CODE_FILE_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Bright Blue
    .ARCHIVE_FILE_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright Red

    .SYNTAX_KEYWORD = FOREGROUND_CYAN | FOREGROUND_INTENSITY, // Bright Cyan
    .SYNTAX_STRING = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .SYNTAX_COMMENT = FOREGROUND_INTENSITY,                   // Gray
    .SYNTAX_NUMBER =
        FOREGROUND_MAGENTA | FOREGROUND_INTENSITY, // Bright Magenta
    .SYNTAX_PREPROCESSOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Yellow

    // ANSI colors for default theme (basic colors, not true full 24-bit colors)
    .ANSI_BASE = "\033[37m",      // White
    .ANSI_SURFACE = "\033[37m",   // White
    .ANSI_OVERLAY = "\033[37m",   // White
    .ANSI_MUTED = "\033[37m",     // White
    .ANSI_SUBTLE = "\033[37m",    // White
    .ANSI_TEXT = "\033[97m",      // Bright White
    .ANSI_LOVE = "\033[91m",      // Bright Red
    .ANSI_GOLD = "\033[93m",      // Bright Yellow
    .ANSI_ROSE = "\033[95m",      // Bright Magenta
    .ANSI_PINE = "\033[92m",      // Bright Green
    .ANSI_FOAM = "\033[96m",      // Bright Cyan
    .ANSI_IRIS = "\033[94m",      // Bright Blue
    .ANSI_HIGHLIGHT = "\033[37m", // White

    // Don't use ANSI colors for default theme
    .use_ansi_colors = FALSE,

    .name = "default"};

static ShellTheme rose_pine_theme = {
    // Legacy console colors (for backward compatibility)
    .PRIMARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White (base)
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // Muted
    .ACCENT_COLOR = FOREGROUND_BLUE, // Blue for git syntax
    .SUCCESS_COLOR = FOREGROUND_GREEN,
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,
    .WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN,

    .HEADER_COLOR = FOREGROUND_RED | FOREGROUND_BLUE,
    .STATUS_BAR_COLOR = 0,
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY,
    .PROMPT_COLOR =
        FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright red for repo name

    .DIRECTORY_COLOR =
        FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright pink for directory

    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN,
    .TEXT_FILE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    .IMAGE_FILE_COLOR = FOREGROUND_BLUE,
    .CODE_FILE_COLOR = FOREGROUND_BLUE,
    .ARCHIVE_FILE_COLOR = FOREGROUND_RED,

    .SYNTAX_KEYWORD = FOREGROUND_RED | FOREGROUND_BLUE,
    .SYNTAX_STRING = FOREGROUND_GREEN,
    .SYNTAX_COMMENT = FOREGROUND_RED | FOREGROUND_GREEN | 0x8,
    .SYNTAX_NUMBER = FOREGROUND_RED,
    .SYNTAX_PREPROCESSOR = FOREGROUND_RED | FOREGROUND_GREEN,

    // ANSI true color definitions - exact Rose Pine colors
    .ANSI_BASE = "\033[38;2;25;23;36m",      // Base color
    .ANSI_SURFACE = "\033[38;2;31;29;46m",   // Surface color
    .ANSI_OVERLAY = "\033[38;2;38;35;58m",   // Overlay color
    .ANSI_MUTED = "\033[38;2;110;106;134m",  // Muted color
    .ANSI_SUBTLE = "\033[38;2;144;140;170m", // Subtle color
    .ANSI_TEXT = "\033[38;2;224;222;244m",   // Text color
    .ANSI_LOVE = "\033[38;2;235;111;146m",   // Love/Red color
    .ANSI_GOLD = "\033[38;2;246;193;119m",   // Gold color
    // Updated Rose/pink color to a soft peach
    .ANSI_ROSE = "\033[38;2;255;195;195m",   // Soft Peach
    .ANSI_PINE = "\033[38;2;49;116;143m",    // Pine color
    .ANSI_FOAM = "\033[38;2;156;207;216m",   // Foam color
    .ANSI_IRIS = "\033[38;2;196;167;231m",   // Iris color
    .ANSI_HIGHLIGHT = "\033[38;2;68;65;90m", // Highlight color
    .ANSI_INVALID_COMMAND = "\033[38;2;205;120;120m",

    // Enable ANSI colors for Rose Pine theme
    .use_ansi_colors = TRUE,

    .name = "rose-pine"};

static ShellTheme catppuccin_mocha_theme = {
    // Legacy console colors (for backward compatibility)
    .PRIMARY_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                     FOREGROUND_INTENSITY, // Bright white for text
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White (dimmer)
    .ACCENT_COLOR =
        FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Bright Blue for accent
    .SUCCESS_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,     // Bright Red
    .WARNING_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Yellow

    .HEADER_COLOR = FOREGROUND_RED | FOREGROUND_BLUE |
                    FOREGROUND_INTENSITY, // Mauve color for headers
    .STATUS_BAR_COLOR = 0,                // Black/dark background
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY, // Bright White
    .PROMPT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                    FOREGROUND_INTENSITY, // Blue text for prompt

    .DIRECTORY_COLOR = FOREGROUND_BLUE | FOREGROUND_GREEN |
                       FOREGROUND_INTENSITY, // Teal for directories
    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                        FOREGROUND_INTENSITY, // Yellow for executables
    .TEXT_FILE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                       FOREGROUND_BLUE, // White for text files
    .IMAGE_FILE_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY, // Red for images
    .CODE_FILE_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Blue for code
    .ARCHIVE_FILE_COLOR =
        FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Green for archives

    .SYNTAX_KEYWORD =
        FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Blue for keywords
    .SYNTAX_STRING =
        FOREGROUND_GREEN | FOREGROUND_INTENSITY,            // Green for strings
    .SYNTAX_COMMENT = FOREGROUND_INTENSITY,                 // Gray for comments
    .SYNTAX_NUMBER = FOREGROUND_RED | FOREGROUND_INTENSITY, // Red for numbers
    .SYNTAX_PREPROCESSOR = FOREGROUND_GREEN | FOREGROUND_BLUE |
                           FOREGROUND_INTENSITY, // Cyan for preprocessor

    // ANSI true color definitions for Catppuccin Mocha
    .ANSI_BASE = "\033[38;2;30;30;46m",      // Base color #1e1e2e
    .ANSI_SURFACE = "\033[38;2;49;50;68m",   // Surface color #313244
    .ANSI_OVERLAY = "\033[38;2;69;71;90m",   // Overlay color #45475a
    .ANSI_MUTED = "\033[38;2;186;194;222m",  // Muted color #bac2de (subtext1)
    .ANSI_SUBTLE = "\033[38;2;166;173;200m", // Subtle color #a6adc8 (subtext0)
    .ANSI_TEXT = "\033[38;2;205;214;244m",   // Text color #cdd6f4
    .ANSI_LOVE = "\033[38;2;243;139;168m",   // Love/Red color #f38ba8
    .ANSI_GOLD = "\033[38;2;249;226;175m",   // Gold/Yellow color #f9e2af
    .ANSI_ROSE = "\033[38;2;245;194;231m",   // Pink color #f5c2e7
    .ANSI_PINE = "\033[38;2;166;227;161m",   // Green color #a6e3a1
    .ANSI_FOAM = "\033[38;2;148;226;213m",   // Teal color #94e2d5
    .ANSI_IRIS = "\033[38;2;203;166;247m",   // Mauve/Purple color #cba6f7
    .ANSI_HIGHLIGHT = "\033[38;2;180;190;254m", // Lavender highlight #b4befe

    // Enable ANSI colors for Catppuccin Mocha theme
    .use_ansi_colors = TRUE,

    .name = "catppuccin-mocha"};

// Path to the theme configuration file
char theme_config_path[PATH_MAX];

void init_theme_system(void) {
  // Determine theme config file location in user's home directory
  char *home_dir = getenv("HOME");
  if (home_dir) {
    snprintf(theme_config_path, PATH_MAX, "%s/.lsh_theme", home_dir);
  } else {
    // Fallback to current directory if HOME not available
    strcpy(theme_config_path, ".lsh_theme");
  }

  // Set default theme initially
  memcpy(&current_theme, &default_theme, sizeof(ShellTheme));

  // Try to load theme from config file
  FILE *config_file = fopen(theme_config_path, "r");
  if (config_file) {
    char theme_name[32];
    if (fscanf(config_file, "theme=%31s", theme_name) == 1) {
      // We found a theme name, try to load it
      load_theme(theme_name);
    }
    fclose(config_file);
  } else {
    // Config file doesn't exist, create it with default theme
    config_file = fopen(theme_config_path, "w");
    if (config_file) {
      fprintf(config_file, "theme=default\n");
      fclose(config_file);
    }
  }
}

int load_theme(const char *theme_name) {
  if (strcmp(theme_name, "default") == 0) {
    memcpy(&current_theme, &default_theme, sizeof(ShellTheme));
    return 1;
  } else if (strcmp(theme_name, "rose-pine") == 0) {
    memcpy(&current_theme, &rose_pine_theme, sizeof(ShellTheme));
    return 1;
  } else if (strcmp(theme_name, "catppuccin-mocha") == 0) {
    memcpy(&current_theme, &catppuccin_mocha_theme, sizeof(ShellTheme));
    return 1;
  }

  // Theme not found
  fprintf(stderr, "Theme '%s' not found\n", theme_name);
  return 0;
}

void apply_current_theme(void) {
  // When called, the shell can update any on-screen elements that depend on
  // theme colors. This function would be called when changing themes or at startup.

  // In Linux, we'll always use ANSI colors, as the terminal emulator handles this natively
  if (current_theme.use_ansi_colors) {
    // Use ANSI true color
    printf("%s", current_theme.ANSI_TEXT);
  } else {
    // Use standard ANSI color approximation of the theme's primary color
    // Linux terminals always support basic ANSI colors, so we'll map the Windows color attributes
    // to the closest ANSI colors
    
    int fg = 37; // Default to white text
    
    if (current_theme.PRIMARY_COLOR & 0x01) fg = 34; // Blue
    if (current_theme.PRIMARY_COLOR & 0x02) fg = 32; // Green
    if (current_theme.PRIMARY_COLOR & 0x04) fg = 31; // Red
    
    // Combinations
    if ((current_theme.PRIMARY_COLOR & 0x03) == 0x03) fg = 36; // Cyan (Green + Blue)
    if ((current_theme.PRIMARY_COLOR & 0x05) == 0x05) fg = 35; // Magenta (Red + Blue)
    if ((current_theme.PRIMARY_COLOR & 0x06) == 0x06) fg = 33; // Yellow (Red + Green)
    if ((current_theme.PRIMARY_COLOR & 0x07) == 0x07) fg = 37; // White (Red + Green + Blue)
    
    // Bright variants
    if (current_theme.PRIMARY_COLOR & 0x08) fg += 60; // Add intensity
    
    printf("\033[%dm", fg); // Set the color
  }
}

const ShellTheme *get_current_theme(void) { return &current_theme; }

void list_available_themes(void) {
  printf("Available themes:\n");
  printf("  default    - Standard shell colors\n");
  printf(
      "  rose-pine  - Soothing, warm color scheme with true color support\n");
  printf("  catppuccin-mocha - Deep dark theme with vibrant accent colors");

  // Display which theme is currently active
  printf("\nCurrent theme: %s", current_theme.name);

  // Display ANSI color status for current theme
  if (current_theme.use_ansi_colors) {
    printf(" (using true color)\n");
  } else {
    printf(" (using standard console colors)\n");
  }
}

void init_themes(void) {
  // Call the theme system initialization
  init_theme_system();
  
  // Apply the initial theme
  apply_current_theme();
}

void shutdown_themes(void) {
  // Reset terminal color to default
  printf(ANSI_COLOR_RESET);
  
  // Currently, no other cleanup is needed for the theme system
}

int lsh_theme(char **args) {
  if (args[1] == NULL) {
    // No arguments, show usage info and current theme
    printf("Usage: theme <command> [arguments]\n");
    printf("Commands:\n");
    printf("  list      List available themes\n");
    printf("  set NAME  Set the current theme to NAME\n");
    printf("  show      Show current theme details\n");
    printf("\nCurrent theme: %s", current_theme.name);

    // Display ANSI color status for current theme
    if (current_theme.use_ansi_colors) {
      printf(" (using true color)\n");
    } else {
      printf(" (using standard console colors)\n");
    }

    return 1;
  }

  // Handle subcommands
  if (strcmp(args[1], "list") == 0) {
    list_available_themes();
    return 1;
  } else if (strcmp(args[1], "set") == 0) {
    if (args[2] == NULL) {
      printf("Usage: theme set <theme_name>\n");
      printf("Try 'theme list' to see available themes\n");
      return 1;
    }

    // Try to load the requested theme
    if (load_theme(args[2])) {
      // Update theme config file
      FILE *config_file = fopen(theme_config_path, "w");
      if (config_file) {
        fprintf(config_file, "theme=%s\n", args[2]);
        fclose(config_file);

        // Apply the theme
        apply_current_theme();

        printf("Theme set to '%s'\n", args[2]);

        // Display ANSI color status for the selected theme
        if (current_theme.use_ansi_colors) {
          printf("This theme uses true color for better visual appearance.\n");
        }
      } else {
        fprintf(stderr, "Could not save theme setting\n");
      }
    }
    return 1;
  } else if (strcmp(args[1], "show") == 0) {
    printf("Current theme: %s\n", current_theme.name);

    // Display ANSI color status
    if (current_theme.use_ansi_colors) {
      printf("Using true color mode");

      // Show a color demo if using ANSI colors
      printf("\nColor sample:\n");
      printf("%sBase %sText %sLove %sGold %sRose %sPine %sFoam %sIris%s\n",
             current_theme.ANSI_BASE, current_theme.ANSI_TEXT,
             current_theme.ANSI_LOVE, current_theme.ANSI_GOLD,
             current_theme.ANSI_ROSE, current_theme.ANSI_PINE,
             current_theme.ANSI_FOAM, current_theme.ANSI_IRIS,
             "\033[0m"); // Reset
    } else {
      printf("Using standard console colors\n");
    }

    return 1;
  } else {
    printf("Unknown theme command: %s\n", args[1]);
    printf("Try 'theme list' or 'theme set <name>'\n");
    return 1;
  }

  return 1;
}
char **get_theme_names(int *theme_count) {
  // Define the hardcoded themes
  char *builtin_themes[] = {"default", "rose-pine", "catppuccin-mocha"};
  int builtin_count = sizeof(builtin_themes) / sizeof(builtin_themes[0]);

  // Try to open the theme config file to check for custom themes
  FILE *config_file = fopen(theme_config_path, "r");
  char custom_theme[32];
  int has_custom_theme = 0;

  if (config_file) {
    char theme_line[64];
    while (fgets(theme_line, sizeof(theme_line), config_file)) {
      if (sscanf(theme_line, "theme=%31s", custom_theme) == 1) {
        // Check if this is a custom theme (not one of our builtin themes)
        int is_builtin = 0;
        for (int i = 0; i < builtin_count; i++) {
          if (strcmp(custom_theme, builtin_themes[i]) == 0) {
            is_builtin = 1;
            break;
          }
        }

        if (!is_builtin) {
          has_custom_theme = 1;
          break;
        }
      }
    }
    fclose(config_file);
  }

  // Allocate space for themes (builtin + potential custom theme)
  int total_themes = builtin_count + (has_custom_theme ? 1 : 0);
  char **theme_names = (char **)malloc(total_themes * sizeof(char *));
  if (!theme_names) {
    fprintf(stderr, "lsh: allocation error in get_theme_names\n");
    *theme_count = 0;
    return NULL;
  }

  // Copy builtin theme names
  for (int i = 0; i < builtin_count; i++) {
    theme_names[i] = strdup(builtin_themes[i]);
  }

  // Add custom theme if found
  if (has_custom_theme) {
    theme_names[builtin_count] = strdup(custom_theme);
  }

  *theme_count = total_themes;
  return theme_names;
}
