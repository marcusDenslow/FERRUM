
#include "favorite_cities.h"
#include "builtins.h"
#include "common.h"

// Global variables for favorite cities storage
CityEntry *favorite_cities = NULL;
int favorite_city_count = 0;
int favorite_city_capacity = 0;

// Path to the favorite cities file
char favorite_cities_file_path[PATH_MAX];

void init_favorite_cities(void) {
  // Set initial capacity
  favorite_city_capacity = 10;
  favorite_cities =
      (CityEntry *)malloc(favorite_city_capacity * sizeof(CityEntry));

  if (!favorite_cities) {
    fprintf(stderr, "lsh: allocation error in init_favorite_cities\n");
    return;
  }

  // Determine favorite cities file location - in user's home directory
  char *home_dir = getenv("HOME");
  if (home_dir) {
    snprintf(favorite_cities_file_path, PATH_MAX, "%s/.lsh_favorite_cities",
             home_dir);
  } else {
    // Fallback to current directory if HOME not available
    strcpy(favorite_cities_file_path, ".lsh_favorite_cities");
  }

  // Load favorite cities from file
  load_favorite_cities();

  // Add some default cities if none exist
  if (favorite_city_count == 0) {
    add_favorite_city("New York");
    add_favorite_city("London");
    add_favorite_city("Tokyo");
    add_favorite_city("Paris");
    add_favorite_city("Sydney");
    save_favorite_cities();
  }
}

void cleanup_favorite_cities(void) {
  if (!favorite_cities)
    return;

  free(favorite_cities);
  favorite_cities = NULL;
  favorite_city_count = 0;
  favorite_city_capacity = 0;
}

void shutdown_favorite_cities(void) {
  // Save to file first
  save_favorite_cities();

  // Then clean up resources
  cleanup_favorite_cities();
}

int load_favorite_cities(void) {
  FILE *file = fopen(favorite_cities_file_path, "r");
  if (!file) {
    // File doesn't exist yet - not an error
    return 1;
  }

  // Clear existing favorite cities
  favorite_city_count = 0;

  char line[128];
  int line_number = 0;

  while (fgets(line, sizeof(line), file)) {
    line_number++;

    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    // Skip empty lines and comments
    if (len == 0 || line[0] == '#')
      continue;

    // Add the city
    add_favorite_city(line);
  }

  fclose(file);
  return 1;
}

int save_favorite_cities(void) {
  FILE *file = fopen(favorite_cities_file_path, "w");
  if (!file) {
    fprintf(stderr, "lsh: error: could not save favorite cities to %s\n",
            favorite_cities_file_path);
    return 0;
  }

  // Write header comment
  fprintf(file, "# LSH favorite cities file\n");
  fprintf(file,
          "# Each line represents a favorite city for weather lookups\n\n");

  // Write each city
  for (int i = 0; i < favorite_city_count; i++) {
    fprintf(file, "%s\n", favorite_cities[i].name);
  }

  fclose(file);
  return 1;
}

int add_favorite_city(const char *name) {
  if (!name)
    return 0;

  // Check if we need to resize the array
  if (favorite_city_count >= favorite_city_capacity) {
    favorite_city_capacity *= 2;
    CityEntry *new_cities = (CityEntry *)realloc(
        favorite_cities, favorite_city_capacity * sizeof(CityEntry));
    if (!new_cities) {
      fprintf(stderr, "lsh: allocation error in add_favorite_city\n");
      return 0;
    }
    favorite_cities = new_cities;
  }

  // Check if the city already exists
  for (int i = 0; i < favorite_city_count; i++) {
    if (strcasecmp(favorite_cities[i].name, name) == 0) {
      // City already exists
      return 1;
    }
  }

  // Add new city
  strncpy(favorite_cities[favorite_city_count].name, name,
          sizeof(favorite_cities[favorite_city_count].name) - 1);
  favorite_cities[favorite_city_count]
      .name[sizeof(favorite_cities[favorite_city_count].name) - 1] = '\0';
  favorite_city_count++;

  return 1;
}

int remove_favorite_city(const char *name) {
  if (!name)
    return 0;

  for (int i = 0; i < favorite_city_count; i++) {
    if (strcasecmp(favorite_cities[i].name, name) == 0) {
      // Shift remaining cities
      for (int j = i; j < favorite_city_count - 1; j++) {
        strcpy(favorite_cities[j].name, favorite_cities[j + 1].name);
      }

      favorite_city_count--;
      return 1;
    }
  }

  return 0; // City not found
}

CityEntry *find_favorite_city(const char *name) {
  if (!name)
    return NULL;

  for (int i = 0; i < favorite_city_count; i++) {
    if (strcasecmp(favorite_cities[i].name, name) == 0) {
      return &favorite_cities[i];
    }
  }

  return NULL;
}

char **get_favorite_city_names(int *count) {
  if (favorite_city_count == 0) {
    *count = 0;
    return NULL;
  }

  char **names = (char **)malloc(favorite_city_count * sizeof(char *));
  if (!names) {
    *count = 0;
    return NULL;
  }

  for (int i = 0; i < favorite_city_count; i++) {
    names[i] = strdup(favorite_cities[i].name);
  }

  *count = favorite_city_count;
  return names;
}

int lsh_cities(char **args) {
  if (args[1] == NULL) {
    // No subcommand - show usage
    printf("Usage: cities <command> [arguments]\n");
    printf("Commands:\n");
    printf("  add <city>      Add a city to favorites\n");
    printf("  remove <city>   Remove a city from favorites\n");
    printf("  list            List all favorite cities\n");
    return 1;
  }

  // Handle subcommands
  if (strcmp(args[1], "add") == 0) {
    if (args[2] == NULL) {
      printf("Usage: cities add <city>\n");
      return 1;
    }

    // Combine all arguments after "add" into a single city name
    char city_name[256] = "";
    strncpy(city_name, args[2], sizeof(city_name) - 1);

    for (int i = 3; args[i] != NULL; i++) {
      strncat(city_name, " ", sizeof(city_name) - strlen(city_name) - 1);
      strncat(city_name, args[i], sizeof(city_name) - strlen(city_name) - 1);
    }

    if (add_favorite_city(city_name)) {
      save_favorite_cities();
      printf("Added '%s' to favorite cities\n", city_name);
    }

  } else if (strcmp(args[1], "remove") == 0) {
    if (args[2] == NULL) {
      printf("Usage: cities remove <city>\n");
      return 1;
    }

    // Combine all arguments after "remove" into a single city name
    char city_name[256] = "";
    strncpy(city_name, args[2], sizeof(city_name) - 1);

    for (int i = 3; args[i] != NULL; i++) {
      strncat(city_name, " ", sizeof(city_name) - strlen(city_name) - 1);
      strncat(city_name, args[i], sizeof(city_name) - strlen(city_name) - 1);
    }

    if (remove_favorite_city(city_name)) {
      save_favorite_cities();
      printf("Removed '%s' from favorite cities\n", city_name);
    } else {
      printf("City '%s' not found in favorites\n", city_name);
    }

  } else if (strcmp(args[1], "list") == 0) {
    // List all favorite cities
    if (favorite_city_count == 0) {
      printf("No favorite cities defined\n");
      return 1;
    }

    printf("\nFavorite Cities:\n\n");

    // Display cities in a nice format
    for (int i = 0; i < favorite_city_count; i++) {
      // Print city name with color
      printf(ANSI_COLOR_CYAN "  %s" ANSI_COLOR_RESET "\n",
             favorite_cities[i].name);
    }
    printf("\n");

  } else {
    printf("Unknown command: %s\n", args[1]);
    printf("Available commands: add, remove, list\n");
  }

  return 1;
}
// heklko
