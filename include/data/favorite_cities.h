
#ifndef FAVORITE_CITIES_H
#define FAVORITE_CITIES_H

#include "common.h"

// Structure to represent a favorite city
typedef struct {
  char name[128]; // City name
} CityEntry;

// Initialize favorite cities system
void init_favorite_cities(void);

// Clean up favorite cities system
void cleanup_favorite_cities(void);

// Shutdown favorite cities system (save and cleanup)
void shutdown_favorite_cities(void);

// Load favorite cities from file
int load_favorite_cities(void);

// Save favorite cities to file
int save_favorite_cities(void);

// Add a new favorite city
int add_favorite_city(const char *name);

// Remove a favorite city
int remove_favorite_city(const char *name);

// Find a favorite city by name
CityEntry *find_favorite_city(const char *name);

// Get favorite city names for tab completion
char **get_favorite_city_names(int *count);

// Command handlers
int lsh_cities(char **args); // Main cities command handler

// Global favorite cities array
extern CityEntry *favorite_cities;
extern int favorite_city_count;

#endif // FAVORITE_CITIES_H
