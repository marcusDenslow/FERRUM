
#include "autocorrect.h"
#include "builtins.h"

int levenshtein_distance(const char *s1, const char *s2) {
  int len1 = strlen(s1);
  int len2 = strlen(s2);

  // Create a matrix to store distances
  int **matrix = (int **)malloc((len1 + 1) * sizeof(int *));
  if (!matrix)
    return -1;

  for (int i = 0; i <= len1; i++) {
    matrix[i] = (int *)malloc((len2 + 1) * sizeof(int));
    if (!matrix[i]) {
      for (int j = 0; j < i; j++) {
        free(matrix[j]);
      }
      free(matrix);
      return -1;
    }
  }

  // Initialize the matrix
  for (int i = 0; i <= len1; i++) {
    matrix[i][0] = i;
  }
  for (int j = 0; j <= len2; j++) {
    matrix[0][j] = j;
  }

  // Fill the matrix
  for (int i = 1; i <= len1; i++) {
    for (int j = 1; j <= len2; j++) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      matrix[i][j] = min3(matrix[i - 1][j] + 1,       // deletion
                          matrix[i][j - 1] + 1,       // insertion
                          matrix[i - 1][j - 1] + cost // substitution
      );
    }
  }

  // Get the result and free the matrix
  int result = matrix[len1][len2];
  for (int i = 0; i <= len1; i++) {
    free(matrix[i]);
  }
  free(matrix);

  return result;
}

int min3(int a, int b, int c) {
  int min = a;
  if (b < min)
    min = b;
  if (c < min)
    min = c;
  return min;
}

void init_autocorrect(void) {
  // No initialization required currently
}

void shutdown_autocorrect(void) {
  // No cleanup required currently
}



char **check_for_corrections(char **args) {
  if (!args || !args[0]) {
    return NULL;
  }

  const char *command = args[0];

  // Skip very short commands or commands that start with ./ or /
  if (strlen(command) < 3 || command[0] == '.' || command[0] == '/') {
    return NULL;
  }

  // Don't try to correct arguments, only the command itself
  // Check if the command is valid, if it is, no correction needed
  if (is_valid_command(command)) {
    return NULL;
  }

  // List of common commands to check against
  const char *common_commands[] = {
      "ls",     "cd",      "grep",     "find",   "cat",        "mv",
      "cp",     "rm",      "mkdir",    "rmdir",  "chmod",      "chown",
      "ps",     "top",     "df",       "du",     "free",       "mount",
      "umount", "tar",     "zip",      "unzip",  "ssh",        "scp",
      "ping",   "netstat", "ifconfig", "route",  "traceroute", "wget",
      "curl",   "apt",     "apt-get",  "yum",    "dnf",        "pacman",
      "git",    "make",    "gcc",      "g++",    "python",     "python3",
      "node",   "npm",     "vim",      "nano",   "history",    "clear",
      "exit",   "alias",   "man",      "help",   "touch",      "echo",
      "pwd",    "sudo",    "shutdown", "reboot", NULL};

  // Check built-in commands too
  int best_distance = 3; // Maximum edit distance to consider a correction
  const char *best_match = NULL;

  // First check among built-in commands
  for (int i = 0; i < lsh_num_builtins(); i++) {
    int distance = levenshtein_distance(command, builtin_str[i]);
    if (distance < best_distance) {
      best_distance = distance;
      best_match = builtin_str[i];
    }
  }

  // Then check common commands
  for (int i = 0; common_commands[i] != NULL; i++) {
    int distance = levenshtein_distance(command, common_commands[i]);
    if (distance < best_distance) {
      best_distance = distance;
      best_match = common_commands[i];
    }
  }

  // If we found a good match, suggest it
  if (best_match) {
    printf("Command '%s' not found. Did you mean '%s'?\n", command, best_match);
    // No longer waiting for input - just return NULL
  }

  return NULL;
}

int count_args(char **args) {
  int count = 0;
  while (args[count] != NULL) {
    count++;
  }
  return count;
}
