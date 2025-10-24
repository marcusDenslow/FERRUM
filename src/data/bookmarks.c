
#include "bookmarks.h"
#include <fcntl.h>
#include <termios.h>

// Global variables for bookmark storage
BookmarkEntry *bookmarks = NULL;
int bookmark_count = 0;
int bookmark_capacity = 0;

// Path to the bookmarks file
char bookmarks_file_path[PATH_MAX];

void init_bookmarks(void) {
  // Set initial capacity
  bookmark_capacity = 10;
  bookmarks =
      (BookmarkEntry *)malloc(bookmark_capacity * sizeof(BookmarkEntry));

  if (!bookmarks) {
    fprintf(stderr, "lsh: allocation error in init_bookmarks\n");
    return;
  }

  // Determine bookmarks file location - in user's home directory
  char *home_dir = getenv("HOME");
  if (home_dir) {
    snprintf(bookmarks_file_path, PATH_MAX, "%s/.lsh_bookmarks", home_dir);
  } else {
    // Fallback to current directory if HOME not available
    strcpy(bookmarks_file_path, ".lsh_bookmarks");
  }

  // Load bookmarks from file
  load_bookmarks();
}

void cleanup_bookmarks(void) {
  if (!bookmarks)
    return;

  // Free all bookmark entries
  for (int i = 0; i < bookmark_count; i++) {
    free(bookmarks[i].name);
    free(bookmarks[i].path);
  }

  // Free the array
  free(bookmarks);
  bookmarks = NULL;
  bookmark_count = 0;
  bookmark_capacity = 0;
}

void shutdown_bookmarks(void) {
  // Save bookmarks to file
  save_bookmarks();

  // Clean up
  cleanup_bookmarks();
}

int load_bookmarks(void) {
  FILE *fp = fopen(bookmarks_file_path, "r");
  if (!fp) {
    return 0; // File doesn't exist or can't be opened
  }

  char line[LSH_RL_BUFSIZE];
  char name[LSH_RL_BUFSIZE / 2];
  char path[LSH_RL_BUFSIZE / 2];

  while (fgets(line, sizeof(line), fp)) {
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    // Try to parse using tab as delimiter first
    char *tab = strchr(line, '\t');
    if (tab) {
      // Split at tab
      *tab = '\0';
      strncpy(name, line, sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';
      strncpy(path, tab + 1, sizeof(path) - 1);
      path[sizeof(path) - 1] = '\0';

      // Remove newline if present
      char *newline = strchr(path, '\n');
      if (newline) {
        *newline = '\0';
      }

      add_bookmark(name, path);
    } else {
      // Parse in name=path format as fallback
      if (sscanf(line, "%[^=]=%s", name, path) == 2) {
        add_bookmark(name, path);
      }
    }
  }

  fclose(fp);
  return 1;
}

int save_bookmarks(void) {
  FILE *fp = fopen(bookmarks_file_path, "w");
  if (!fp) {
    fprintf(stderr, "lsh: error saving bookmarks to %s\n", bookmarks_file_path);
    return 0;
  }

  // Write file header
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char timestamp[26];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(fp, "# LSH Bookmarks - Last updated: %s\n\n", timestamp);

  // Write each bookmark
  for (int i = 0; i < bookmark_count; i++) {
    fprintf(fp, "%s\t%s\n", bookmarks[i].name, bookmarks[i].path);
  }

  fclose(fp);
  return 1;
}

int add_bookmark(const char *name, const char *path) {
  if (!name || !path) {
    return 0;
  }

  // Check if bookmark already exists
  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      // Update existing bookmark
      free(bookmarks[i].path);
      bookmarks[i].path = strdup(path);
      return 1;
    }
  }

  // Check if we need to expand the array
  if (bookmark_count >= bookmark_capacity) {
    bookmark_capacity *= 2;
    BookmarkEntry *new_bookmarks = (BookmarkEntry *)realloc(
        bookmarks, bookmark_capacity * sizeof(BookmarkEntry));
    if (!new_bookmarks) {
      fprintf(stderr, "lsh: allocation error in add_bookmark\n");
      return 0;
    }
    bookmarks = new_bookmarks;
  }

  // Add new bookmark
  bookmarks[bookmark_count].name = strdup(name);
  bookmarks[bookmark_count].path = strdup(path);
  bookmark_count++;

  return 1;
}

int remove_bookmark(const char *name) {
  if (!name) {
    return 0;
  }

  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      // Free memory for this bookmark
      free(bookmarks[i].name);
      free(bookmarks[i].path);

      // Shift remaining bookmarks down
      for (int j = i; j < bookmark_count - 1; j++) {
        bookmarks[j] = bookmarks[j + 1];
      }

      bookmark_count--;
      return 1;
    }
  }

  return 0; // Bookmark not found
}

BookmarkEntry *find_bookmark(const char *name) {
  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      return &bookmarks[i];
    }
  }
  return NULL;
}

int lsh_bookmark(char **args) {
  char cwd[PATH_MAX];

  // No arguments - add bookmark for current directory
  if (args[1] == NULL) {
    printf("Usage: bookmark <name> [path]\n");
    printf("If path is omitted, the current directory is used.\n");
    return 1;
  }

  // Get current working directory if no path is specified
  if (args[2] == NULL) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      perror("lsh: getcwd");
      return 1;
    }
    add_bookmark(args[1], cwd);
    printf("Bookmark added: %s -> %s\n", args[1], cwd);
  } else {
    add_bookmark(args[1], args[2]);
    printf("Bookmark added: %s -> %s\n", args[1], args[2]);
  }

  // Save bookmarks to file
  save_bookmarks();

  return 1;
}

int lsh_bookmarks(char **args) {
  if (bookmark_count == 0) {
    printf("No bookmarks defined.\n");
    printf("Use 'bookmark <name> [path]' to add a bookmark.\n");
    return 1;
  }

  // Check if we should use a pager
  if (bookmark_count > 20) {
    // Check for neovim first
    FILE *test_nvim = popen("which nvim 2>/dev/null", "r");
    if (test_nvim) {
      char buffer[128];
      if (fgets(buffer, sizeof(buffer), test_nvim) != NULL) {
        pclose(test_nvim);
        FILE *pager = popen("nvim -R -c 'set nonumber' -", "w");
        if (pager) {
          fprintf(pager, "LSH Bookmarks:\n\n");
          for (int i = 0; i < bookmark_count; i++) {
            fprintf(pager, "  %s -> %s\n", bookmarks[i].name,
                    bookmarks[i].path);
          }
          pclose(pager);
          return 1;
        }
      }
      pclose(test_nvim);

      // Try vim
      FILE *test_vim = popen("which vim 2>/dev/null", "r");
      if (test_vim) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), test_vim) != NULL) {
          pclose(test_vim);
          FILE *pager = popen("vim -R -c 'set nonumber' -", "w");
          if (pager) {
            fprintf(pager, "LSH Bookmarks:\n\n");
            for (int i = 0; i < bookmark_count; i++) {
              fprintf(pager, "  %s -> %s\n", bookmarks[i].name,
                      bookmarks[i].path);
            }
            pclose(pager);
            return 1;
          }
        }
        pclose(test_vim);
      }
    }
  }

  // Simple output if no pager available or bookmark count is small
  printf("LSH Bookmarks:\n\n");

  // Use ANSI escape codes for colored output
  for (int i = 0; i < bookmark_count; i++) {
    printf("  " ANSI_COLOR_GREEN "%s" ANSI_COLOR_RESET " -> %s\n",
           bookmarks[i].name, bookmarks[i].path);
  }

  return 1;
}

int lsh_goto(char **args) {
  if (args[1] == NULL) {
    printf("Usage: goto <bookmark_name>\n");
    return 1;
  }

  BookmarkEntry *bookmark = find_bookmark(args[1]);
  if (bookmark) {
    if (chdir(bookmark->path) != 0) {
      perror("lsh: chdir");
      return 1;
    }
    printf("Changed directory to: %s\n", bookmark->path);
  } else {
    printf("Bookmark '%s' not found.\n", args[1]);
  }

  return 1;
}

int lsh_unbookmark(char **args) {
  if (args[1] == NULL) {
    printf("Usage: unbookmark <bookmark_name>\n");
    return 1;
  }

  if (remove_bookmark(args[1])) {
    printf("Bookmark '%s' removed.\n", args[1]);
    save_bookmarks();
  } else {
    printf("Bookmark '%s' not found.\n", args[1]);
  }

  return 1;
}

char **get_bookmark_names(int *count) {
  if (bookmark_count == 0 || !count) {
    *count = 0;
    return NULL;
  }

  char **names = (char **)malloc(bookmark_count * sizeof(char *));
  if (!names) {
    *count = 0;
    return NULL;
  }

  for (int i = 0; i < bookmark_count; i++) {
    names[i] = strdup(bookmarks[i].name);
  }

  *count = bookmark_count;
  return names;
}

char *find_matching_bookmark(const char *partial_name) {
  if (!partial_name || strlen(partial_name) == 0) {
    return NULL;
  }

  // If only one bookmark exists, return it if there's any match
  if (bookmark_count == 1) {
    return strdup(bookmarks[0].name);
  }

  // Look for partial matches
  for (int i = 0; i < bookmark_count; i++) {
    if (strncasecmp(bookmarks[i].name, partial_name, strlen(partial_name)) ==
        0) {
      return strdup(bookmarks[i].name);
    }
  }

  return NULL;
}
// jeg elsker deg pia!!!!!
