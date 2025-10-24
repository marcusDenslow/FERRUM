
#include "aliases.h"
#include "builtins.h"
#include <sys/stat.h>
#include <time.h>

// Global variables for alias storage
AliasEntry *aliases = NULL;
int alias_count = 0;
int alias_capacity = 0;
int loading_aliases = 0; // Flag to prevent saving during loading

// Path to the aliases file
char aliases_file_path[PATH_MAX];

void init_aliases(void) {
  // Set initial capacity
  alias_capacity = 10;
  aliases = (AliasEntry *)malloc(alias_capacity * sizeof(AliasEntry));

  if (!aliases) {
    fprintf(stderr, "lsh: allocation error in init_aliases\n");
    return;
  }

  // Determine aliases file location - in user's home directory
  char *home_dir = getenv("HOME");
  if (home_dir) {
    snprintf(aliases_file_path, PATH_MAX, "%s/.lsh_aliases", home_dir);
  } else {
    // Fallback to current directory if HOME not available
    strcpy(aliases_file_path, ".lsh_aliases");
  }

  // Load aliases from file
  load_aliases();
}

void cleanup_aliases(void) {
  if (!aliases)
    return;

  // Free all alias entries
  for (int i = 0; i < alias_count; i++) {
    free(aliases[i].name);
    free(aliases[i].command);
  }

  // Free the array
  free(aliases);
  aliases = NULL;
  alias_count = 0;
  alias_capacity = 0;
}

void shutdown_aliases(void) {
  // Clean up (don't save here since we save immediately on changes)
  cleanup_aliases();
}

int load_aliases(void) {
  FILE *fp = fopen(aliases_file_path, "r");
  if (!fp) {
    return 0; // File doesn't exist or can't be opened
  }

  // Set loading flag to prevent saving during loading
  loading_aliases = 1;

  char line[LSH_RL_BUFSIZE];
  char name[LSH_RL_BUFSIZE / 2];
  char command[LSH_RL_BUFSIZE / 2];

  while (fgets(line, sizeof(line), fp)) {
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    // Parse alias definitions (name=command)
    char *equals = strchr(line, '=');
    if (equals) {
      *equals = '\0';
      char *line_name = line;
      char *line_command = equals + 1;

      // Remove newline from command if present
      char *newline = strchr(line_command, '\n');
      if (newline) {
        *newline = '\0';
      }

      // Trim whitespace
      while (*line_name == ' ' || *line_name == '\t')
        line_name++;
      while (*line_command == ' ' || *line_command == '\t')
        line_command++;

      if (*line_name && *line_command) {
        add_alias(line_name, line_command);
      }
    }
  }

  // Clear loading flag
  loading_aliases = 0;

  fclose(fp);
  return 1;
}

int save_aliases(void) {
  FILE *fp = fopen(aliases_file_path, "w");
  if (!fp) {
    fprintf(stderr, "lsh: error saving aliases to %s\n", aliases_file_path);
    return 0;
  }

  // Write file header
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char timestamp[26];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(fp, "# LSH Aliases - Last updated: %s\n\n", timestamp);

  // Write each alias
  for (int i = 0; i < alias_count; i++) {
    fprintf(fp, "%s=%s\n", aliases[i].name, aliases[i].command);
  }

  fclose(fp);
  return 1;
}

int add_alias(const char *name, const char *command) {
  if (!name || !command) {
    return 0;
  }

  // Check if alias already exists, update it if it does
  for (int i = 0; i < alias_count; i++) {
    if (strcmp(aliases[i].name, name) == 0) {
      // Update existing alias
      free(aliases[i].command);
      aliases[i].command = strdup(command);
      // Save aliases immediately after updating (unless loading)
      if (!loading_aliases) {
        save_aliases();
      }
      return 1;
    }
  }

  // Check if we need to expand the array
  if (alias_count >= alias_capacity) {
    alias_capacity *= 2;
    AliasEntry *new_aliases =
        (AliasEntry *)realloc(aliases, alias_capacity * sizeof(AliasEntry));
    if (!new_aliases) {
      fprintf(stderr, "lsh: allocation error in add_alias\n");
      return 0;
    }
    aliases = new_aliases;
  }

  // Add new alias
  aliases[alias_count].name = strdup(name);
  aliases[alias_count].command = strdup(command);
  alias_count++;

  // Save aliases immediately after adding (unless loading)
  if (!loading_aliases) {
    save_aliases();
  }

  return 1;
}

int remove_alias(const char *name) {
  if (!name) {
    return 0;
  }

  for (int i = 0; i < alias_count; i++) {
    if (strcmp(aliases[i].name, name) == 0) {
      // Free memory for this alias
      free(aliases[i].name);
      free(aliases[i].command);

      // Shift remaining aliases down
      for (int j = i; j < alias_count - 1; j++) {
        aliases[j] = aliases[j + 1];
      }

      alias_count--;
      // Save aliases immediately after removing (unless loading)
      if (!loading_aliases) {
        save_aliases();
      }
      return 1;
    }
  }

  return 0; // Alias not found
}

AliasEntry *find_alias(const char *name) {
  for (int i = 0; i < alias_count; i++) {
    if (strcmp(aliases[i].name, name) == 0) {
      return &aliases[i];
    }
  }
  return NULL;
}

char *expand_aliases(const char *command) {
  if (!command) {
    return NULL;
  }

  char *result = strdup(command);
  if (!result)
    return NULL;

  // Copy the command for tokenization
  char *cmd_copy = strdup(result);
  if (!cmd_copy) {
    free(result);
    return NULL;
  }

  // Extract the first word (command name)
  char *token = strtok(cmd_copy, " \t");
  if (!token) {
    free(cmd_copy);
    return result;
  }

  // Look up the alias
  AliasEntry *alias = find_alias(token);
  if (alias) {
    // Get the args after the command
    char *args = strchr(command, ' ');

    // Allocate new buffer for expanded command
    free(result);
    if (args) {
      // Concatenate alias command and original args
      result = malloc(strlen(alias->command) + strlen(args) + 1);
      if (result) {
        strcpy(result, alias->command);
        strcat(result, args);
      }
    } else {
      // No args, just use alias command
      result = strdup(alias->command);
    }
  }

  free(cmd_copy);
  return result;
}

int lsh_alias(char **args) {
  // No arguments - list all aliases
  if (args[1] == NULL) {
    for (int i = 0; i < alias_count; i++) {
      printf("alias %s='%s'\n", aliases[i].name, aliases[i].command);
    }
    return 1;
  }

  // Pre-defined aliases
  if (strcmp(args[1], "vim-mode") == 0) {
    // Configure aliases for Vim-like operation
    add_alias("v", "vim");

    // Check for neovim first
    FILE *test_nvim = popen("which nvim 2>/dev/null", "r");
    if (test_nvim) {
      char buffer[128];
      if (fgets(buffer, sizeof(buffer), test_nvim) != NULL) {
        // Neovim found, use it as preferred editor
        pclose(test_nvim);
        add_alias("e", "nvim");
        printf("Vim mode activated (using Neovim)\n");
        return 1;
      }
      pclose(test_nvim);

      // Check for regular vim
      FILE *test_vim = popen("which vim 2>/dev/null", "r");
      if (test_vim) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), test_vim) != NULL) {
          // Vim found
          pclose(test_vim);
          add_alias("e", "vim");
          printf("Vim mode activated\n");
          return 1;
        }
        pclose(test_vim);
      }
    }

    // Fallback to vi if neither vim nor neovim is found
    add_alias("e", "vi");
    printf("Vim mode activated (using vi)\n");
    return 1;
  } else if (strcmp(args[1], "emacs-mode") == 0) {
    // Configure aliases for Emacs-like operation
    FILE *test_emacs = popen("which emacs 2>/dev/null", "r");
    if (test_emacs) {
      char buffer[128];
      if (fgets(buffer, sizeof(buffer), test_emacs) != NULL) {
        pclose(test_emacs);
        add_alias("e", "emacs -nw");
        printf("Emacs mode activated\n");
        return 1;
      }
      pclose(test_emacs);
    }

    printf("Emacs not found. Emacs mode not activated.\n");
    return 1;
  }

  // Format: alias name=command
  char *equals = strchr(args[1], '=');
  if (equals) {
    // Split at equals sign
    *equals = '\0';
    char *name = args[1];
    char *command = equals + 1;

    // Remove quotes around command if present
    if (*command == '\'' || *command == '"') {
      command++;
      char *end = command + strlen(command) - 1;
      if (*end == '\'' || *end == '"') {
        *end = '\0';
      }
    }

    add_alias(name, command);
    printf("Alias added: %s='%s'\n", name, command);
  } else if (args[2] != NULL) {
    // Format: alias name command
    add_alias(args[1], args[2]);
    printf("Alias added: %s='%s'\n", args[1], args[2]);
  } else {
    // Just a name - show this specific alias
    AliasEntry *alias = find_alias(args[1]);
    if (alias) {
      printf("alias %s='%s'\n", alias->name, alias->command);
    } else {
      printf("Alias '%s' not found\n", args[1]);
    }
  }

  return 1;
}

int lsh_unalias(char **args) {
  if (args[1] == NULL) {
    printf("unalias: missing argument\n");
    return 1;
  }

  if (remove_alias(args[1])) {
    printf("Alias '%s' removed\n", args[1]);
  } else {
    printf("Alias '%s' not found\n", args[1]);
  }

  return 1;
}

int lsh_aliases(char **args) {
  for (int i = 0; i < alias_count; i++) {
    printf("alias %s='%s'\n", aliases[i].name, aliases[i].command);
  }
  return 1;
}

char **get_alias_names(int *count) {
  if (alias_count == 0 || !count) {
    *count = 0;
    return NULL;
  }

  char **names = (char **)malloc(alias_count * sizeof(char *));
  if (!names) {
    *count = 0;
    return NULL;
  }

  for (int i = 0; i < alias_count; i++) {
    names[i] = strdup(aliases[i].name);
  }

  *count = alias_count;
  return names;
}

char **expand_alias(char **args) {
  if (!args || !args[0]) {
    return NULL;
  }

  // Check if the command is an alias
  AliasEntry *alias = find_alias(args[0]);
  if (!alias) {
    return NULL;
  }

  // Count tokens in alias command
  char *alias_copy = strdup(alias->command);
  if (!alias_copy) {
    return NULL;
  }

  char **alias_tokens = malloc(LSH_TOK_BUFSIZE * sizeof(char *));
  if (!alias_tokens) {
    free(alias_copy);
    return NULL;
  }

  char *token, *rest = alias_copy;
  int token_count = 0;

  while ((token = strtok_r(rest, " \t", &rest)) != NULL) {
    alias_tokens[token_count++] = strdup(token);

    if (token_count >= LSH_TOK_BUFSIZE) {
      fprintf(stderr, "lsh: alias expansion: too many tokens\n");
      break;
    }
  }

  free(alias_copy);

  // Count original arguments
  int arg_count = 0;
  while (args[arg_count] != NULL) {
    arg_count++;
  }

  // Allocate space for expanded command
  char **expanded_args = malloc((token_count + arg_count) * sizeof(char *));
  if (!expanded_args) {
    for (int i = 0; i < token_count; i++) {
      free(alias_tokens[i]);
    }
    free(alias_tokens);
    return NULL;
  }

  // Copy alias tokens
  for (int i = 0; i < token_count; i++) {
    expanded_args[i] = alias_tokens[i];
  }

  // Copy remaining arguments (skip the command)
  for (int i = 1; i < arg_count; i++) {
    expanded_args[token_count + i - 1] = strdup(args[i]);
  }

  // Null-terminate the array
  expanded_args[token_count + arg_count - 1] = NULL;

  free(alias_tokens);
  return expanded_args;
}
