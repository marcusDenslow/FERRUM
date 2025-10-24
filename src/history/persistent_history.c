
#include "persistent_history.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Global variables for persistent history
PersistentHistoryEntry *history_entries = NULL;
int history_size = 0;
static int history_capacity = 0;
static int history_position = 0;

// Global variables for frequency tracking
CommandFrequency *command_frequencies = NULL;
int frequency_count = 0;
static int frequency_capacity = 0;

// Paths for history and frequency files
static char history_file_path[PATH_MAX];
static char frequency_file_path[PATH_MAX];

void init_persistent_history(void) {
  // Allocate initial history capacity
  history_capacity = PERSISTENT_HISTORY_SIZE;
  history_entries = (PersistentHistoryEntry *)malloc(
      history_capacity * sizeof(PersistentHistoryEntry));
  if (!history_entries) {
    fprintf(stderr, "Failed to allocate memory for history entries\n");
    return;
  }

  // Allocate initial frequency capacity
  frequency_capacity = 100;
  command_frequencies =
      (CommandFrequency *)malloc(frequency_capacity * sizeof(CommandFrequency));
  if (!command_frequencies) {
    fprintf(stderr, "Failed to allocate memory for command frequencies\n");
    free(history_entries);
    history_entries = NULL;
    return;
  }

  // Initialize history and frequency counters
  history_size = 0;
  history_position = -1;
  frequency_count = 0;

  // Set up file paths
  char *home_dir = getenv("HOME");
  if (home_dir) {
    // Create .lsh directory if it doesn't exist
    char lsh_dir[PATH_MAX];
    snprintf(lsh_dir, sizeof(lsh_dir), "%s/.lsh", home_dir);

    struct stat st = {0};
    if (stat(lsh_dir, &st) == -1) {
      mkdir(lsh_dir, 0700);
    }

    snprintf(history_file_path, sizeof(history_file_path), "%s/.lsh/history",
             home_dir);
    snprintf(frequency_file_path, sizeof(frequency_file_path),
             "%s/.lsh/frequency", home_dir);
  } else {
    // Fallback to current directory
    strcpy(history_file_path, ".lsh_history");
    strcpy(frequency_file_path, ".lsh_frequency");
  }

  // Load history and frequency data
  load_history_from_file();
  load_frequencies_from_file();
}

char *get_most_recent_history_match(const char *prefix) {
  if (!prefix || !history_entries || strlen(prefix) == 0) {
    return NULL;
  }

  size_t prefix_len = strlen(prefix);

  // Search from most recent to oldest (reverse order)
  // Find the first (most recent) command that starts with the exact prefix
  for (int i = history_size - 1; i >= 0; i--) {
    if (history_entries[i].command &&
        strncasecmp(history_entries[i].command, prefix, prefix_len) == 0 &&
        strlen(history_entries[i].command) > prefix_len) {

      // Make sure it's an exact prefix match (not a substring)
      // For "git s", we want "git status", not "git branch" where "git s" is not a real prefix
      const char *cmd = history_entries[i].command;

      // If prefix ends with a space, any command starting with prefix is good
      if (prefix_len > 0 && prefix[prefix_len - 1] == ' ') {
        return strdup(cmd);
      }

      // If prefix doesn't end with space, make sure the next char in command
      // is either a space or continues the word we're typing
      if (cmd[prefix_len] == ' ' || isalnum(cmd[prefix_len]) || cmd[prefix_len] == '-') {
        return strdup(cmd);
      }
    }
  }
  return NULL;
}

void cleanup_persistent_history(void) {
  if (history_entries) {
    for (int i = 0; i < history_size; i++) {
      free(history_entries[i].command);
    }
    free(history_entries);
    history_entries = NULL;
  }

  if (command_frequencies) {
    for (int i = 0; i < frequency_count; i++) {
      free(command_frequencies[i].command);
    }
    free(command_frequencies);
    command_frequencies = NULL;
  }

  history_size = 0;
  history_capacity = 0;
  frequency_count = 0;
  frequency_capacity = 0;
}

void shutdown_persistent_history(void) {
  save_history_to_file();
  save_frequencies_to_file();
  cleanup_persistent_history();
}

void add_to_history(const char *command) {
  if (!command || !*command || !history_entries) {
    return; // Skip empty commands
  }

  // Check for duplicates (don't add the same command twice in a row)
  if (history_size > 0 &&
      strcmp(history_entries[history_size - 1].command, command) == 0) {
    return;
  }

  // Update command frequency
  update_command_frequency(command);

  // Add to history
  if (history_size >= history_capacity) {
    // History is full, shift entries
    free(history_entries[0].command);
    memmove(&history_entries[0], &history_entries[1],
            (history_capacity - 1) * sizeof(PersistentHistoryEntry));
    history_size = history_capacity - 1;
  }

  // Add new entry
  history_entries[history_size].command = strdup(command);
  history_entries[history_size].timestamp = time(NULL);
  history_size++;

  // Reset history position for navigation
  history_position = -1;
}

void update_command_frequency(const char *command) {
  if (!command || !*command || !command_frequencies) {
    return;
  }

  // Check if command exists in frequencies
  for (int i = 0; i < frequency_count; i++) {
    if (strcmp(command_frequencies[i].command, command) == 0) {
      // Update existing entry
      command_frequencies[i].count++;
      return;
    }
  }

  // Add new frequency entry
  if (frequency_count >= frequency_capacity) {
    // Expand capacity
    frequency_capacity *= 2;
    CommandFrequency *new_freq = (CommandFrequency *)realloc(
        command_frequencies, frequency_capacity * sizeof(CommandFrequency));
    if (!new_freq) {
      fprintf(stderr, "Failed to allocate memory for command frequencies\n");
      return;
    }
    command_frequencies = new_freq;
  }

  // Add new entry
  command_frequencies[frequency_count].command = strdup(command);
  command_frequencies[frequency_count].count = 1;
  frequency_count++;
}

void save_history_to_file(void) {
  if (!history_entries || history_size == 0) {
    return;
  }

  FILE *fp = fopen(history_file_path, "w");
  if (!fp) {
    fprintf(stderr, "Failed to open history file for writing: %s\n",
            history_file_path);
    return;
  }

  // Write version and metadata
  fprintf(fp, "# LSH Persistent History\n");
  fprintf(fp, "# Version: 1.0\n");
  fprintf(fp, "# Format: timestamp command\n\n");

  // Write entries
  for (int i = 0; i < history_size; i++) {
    fprintf(fp, "%ld %s\n", (long)history_entries[i].timestamp,
            history_entries[i].command);
  }

  fclose(fp);
}

void load_history_from_file(void) {
  FILE *fp = fopen(history_file_path, "r");
  if (!fp) {
    return; // File doesn't exist or can't be opened
  }

  char line[1024];
  time_t timestamp;
  char command[900]; // Allow space for timestamp and whitespace

  // Skip header lines
  while (fgets(line, sizeof(line), fp) && line[0] == '#') {
    // Skip header line
  }

  // Clear history
  for (int i = 0; i < history_size; i++) {
    free(history_entries[i].command);
  }
  history_size = 0;

  // Read entries
  while (fgets(line, sizeof(line), fp)) {
    // Parse line: timestamp command
    if (sscanf(line, "%ld %[^\n]", &timestamp, command) == 2) {
      if (history_size < history_capacity) {
        history_entries[history_size].command = strdup(command);
        history_entries[history_size].timestamp = timestamp;
        history_size++;
      }
    }
  }

  fclose(fp);
}

void save_frequencies_to_file(void) {
  if (!command_frequencies || frequency_count == 0) {
    return;
  }

  FILE *fp = fopen(frequency_file_path, "w");
  if (!fp) {
    fprintf(stderr, "Failed to open frequency file for writing: %s\n",
            frequency_file_path);
    return;
  }

  // Write version and metadata
  fprintf(fp, "# LSH Command Frequencies\n");
  fprintf(fp, "# Version: 1.0\n");
  fprintf(fp, "# Format: count command\n\n");

  // Write entries
  for (int i = 0; i < frequency_count; i++) {
    fprintf(fp, "%d %s\n", command_frequencies[i].count,
            command_frequencies[i].command);
  }

  fclose(fp);
}

void load_frequencies_from_file(void) {
  FILE *fp = fopen(frequency_file_path, "r");
  if (!fp) {
    return; // File doesn't exist or can't be opened
  }

  char line[1024];
  int count;
  char command[900]; // Allow space for count and whitespace

  // Skip header lines
  while (fgets(line, sizeof(line), fp) && line[0] == '#') {
    // Skip header line
  }

  // Clear frequencies
  for (int i = 0; i < frequency_count; i++) {
    free(command_frequencies[i].command);
  }
  frequency_count = 0;

  // Read entries
  while (fgets(line, sizeof(line), fp)) {
    // Parse line: count command
    if (sscanf(line, "%d %[^\n]", &count, command) == 2) {
      if (frequency_count < frequency_capacity) {
        command_frequencies[frequency_count].command = strdup(command);
        command_frequencies[frequency_count].count = count;
        frequency_count++;
      }
    }
  }

  fclose(fp);
}

PersistentHistoryEntry *get_history_entry(int index) {
  if (index < 0 || index >= history_size) {
    return NULL;
  }
  return &history_entries[index];
}

int get_history_count(void) { return history_size; }

char *find_best_frequency_match(const char *prefix) {
  if (!prefix || !*prefix || !command_frequencies) {
    return NULL;
  }

  int best_freq = 0;
  int best_index = -1;

  // Find the most frequent command that starts with the prefix
  for (int i = 0; i < frequency_count; i++) {
    if (strncasecmp(command_frequencies[i].command, prefix, strlen(prefix)) ==
        0) {
      if (command_frequencies[i].count > best_freq) {
        best_freq = command_frequencies[i].count;
        best_index = i;
      }
    }
  }

  if (best_index >= 0) {
    return strdup(command_frequencies[best_index].command);
  }

  return NULL;
}

void debug_print_frequencies(void) {
  printf("Command Frequencies:\n");
  for (int i = 0; i < frequency_count; i++) {
    printf("%3d: %s (%d)\n", i + 1, command_frequencies[i].command,
           command_frequencies[i].count);
  }
}

char *_stristr(const char *haystack, const char *needle) {
  if (!haystack || !needle)
    return NULL;

  char *h = strdup(haystack);
  char *n = strdup(needle);

  if (!h || !n) {
    if (h)
      free(h);
    if (n)
      free(n);
    return NULL;
  }

  // Convert both strings to lowercase
  for (char *p = h; *p; p++)
    *p = tolower(*p);
  for (char *p = n; *p; p++)
    *p = tolower(*p);

  char *result = strstr(h, n);
  char *original = NULL;

  // If found, map back to original string position
  if (result) {
    original = (char *)haystack + (result - h);
  }

  free(h);
  free(n);
  return original;
}

char *get_previous_history_entry(int *position) {
  if (!history_entries || history_size == 0) {
    return NULL;
  }

  if (*position < 0) {
    // First time accessing history, start from the most recent entry
    *position = history_size - 1;
  } else if (*position > 0) {
    // Move to previous entry
    (*position)--;
  } else {
    // Already at the oldest entry, can't go back further
    return history_entries[0].command;
  }

  return history_entries[*position].command;
}

char *get_next_history_entry(int *position) {
  if (!history_entries || history_size == 0 || *position < 0) {
    return NULL;
  }

  if (*position < history_size - 1) {
    // Move to next (more recent) entry
    (*position)++;
    return history_entries[*position].command;
  } else {
    // We've reached the end of history, return NULL to indicate
    // user should get an empty prompt
    *position = -1;
    return NULL;
  }
}

char **get_matching_history_entries(const char *prefix) {
  if (!prefix || !history_entries) {
    return NULL;
  }

  // Count matching entries
  int matches = 0;
  for (int i = 0; i < history_size; i++) {
    if (strncasecmp(history_entries[i].command, prefix, strlen(prefix)) == 0) {
      matches++;
    }
  }

  if (matches == 0) {
    return NULL;
  }

  // Allocate result array
  char **result = (char **)malloc((matches + 1) * sizeof(char *));
  if (!result) {
    return NULL;
  }

  // Fill the array with matching commands
  int count = 0;
  for (int i = 0; i < history_size && count < matches; i++) {
    if (strncasecmp(history_entries[i].command, prefix, strlen(prefix)) == 0) {
      result[count++] = strdup(history_entries[i].command);
    }
  }
  result[count] = NULL;

  return result;
}

void free_matching_entries(char **entries) {
  if (!entries) {
    return;
  }

  for (int i = 0; entries[i] != NULL; i++) {
    free(entries[i]);
  }
  free(entries);
}
