
#ifndef ALIASES_H
#define ALIASES_H

#include "common.h"

// Structure to represent an alias
typedef struct AliasEntry {
    char *name;     // Alias name (command to type)
    char *command;  // The command it expands to
} AliasEntry;

// Initialize alias system
void init_aliases(void);

// Clean up alias system
void cleanup_aliases(void);

// Shutdown the alias system
void shutdown_aliases(void);

// Load aliases from file
int load_aliases(void);

// Save aliases to file
int save_aliases(void);

// Add a new alias
int add_alias(const char *name, const char *command);

// Remove an alias
int remove_alias(const char *name);

// Find an alias by name
AliasEntry* find_alias(const char *name);

// Expand a command line by replacing aliases
char* expand_aliases(const char *command);

// Command handler for the "alias" command
int lsh_alias(char **args);

// Command handler for the "unalias" command
int lsh_unalias(char **args);

// Command handler to list all aliases
int lsh_aliases(char **args);

// Get alias names for tab completion
char** get_alias_names(int *count);

// Expand alias in args array
char** expand_alias(char **args);

// Global variables for alias storage
extern AliasEntry *aliases;
extern int alias_count;

#endif // ALIASES_H
