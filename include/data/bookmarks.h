
#ifndef BOOKMARKS_H
#define BOOKMARKS_H

#include "common.h"

// Structure to represent a bookmark
typedef struct {
  char *name; // Bookmark name
  char *path; // Directory path
} BookmarkEntry;

// Initialize bookmark system
void init_bookmarks(void);

// Clean up bookmark system
void cleanup_bookmarks(void);

// Shutdown the bookmark system
void shutdown_bookmarks(void);

// Load bookmarks from file
int load_bookmarks(void);

// Save bookmarks to file
int save_bookmarks(void);

// Add a new bookmark
int add_bookmark(const char *name, const char *path);

// Remove a bookmark by name
int remove_bookmark(const char *name);

// Find a bookmark by name
BookmarkEntry *find_bookmark(const char *name);

// Command handlers
int lsh_bookmark(char **args);   // Add a bookmark
int lsh_bookmarks(char **args);  // List all bookmarks
int lsh_goto(char **args);       // Jump to a bookmark
int lsh_unbookmark(char **args); // Remove a bookmark

// Get bookmarked directories for tab completion
char **get_bookmark_names(int *count);

// Find a matching bookmark by partial name
char *find_matching_bookmark(const char *partial_name);

// Global bookmarks array
extern BookmarkEntry *bookmarks;
extern int bookmark_count;

#endif // BOOKMARKS_H