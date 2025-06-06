/**
 * git_integration.c
 * Implementation of Git repository detection and information
 */

#include "git_integration.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * Initialize Git integration
 */
void init_git_integration(void) {
  // No initialization needed for Linux
}

/**
 * Check if the current directory is in a Git repository and get branch info
 */
int get_git_branch(char *branch_name, size_t buffer_size, int *is_dirty) {
  char git_dir[PATH_MAX] = "";
  char cmd[PATH_MAX] = "";
  FILE *fp;
  int status = 0;

  // Initialize output parameters
  if (branch_name && buffer_size > 0) {
    branch_name[0] = '\0';
  }
  if (is_dirty) {
    *is_dirty = 0;
  }

  // First check if .git directory exists (faster than running git command)
  strcpy(git_dir, ".git");
  struct stat st;
  if (stat(git_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
    // Try to find .git in parent directories
    // (Note: this is more complex, simplified here)
    return 0;
  }

  // Get current branch
  if (branch_name && buffer_size > 0) {
    strcpy(cmd, "git rev-parse --abbrev-ref HEAD 2>/dev/null");
    fp = popen(cmd, "r");
    if (fp) {
      if (fgets(branch_name, buffer_size, fp) != NULL) {
        // Remove trailing newline
        char *newline = strchr(branch_name, '\n');
        if (newline) {
          *newline = '\0';
        }
        status = 1;
      }
      pclose(fp);
    }
  }

  // Check if working directory is dirty
  if (is_dirty && status) {
    strcpy(cmd, "git status --porcelain 2>/dev/null");
    fp = popen(cmd, "r");
    if (fp) {
      char ch;
      if ((ch = fgetc(fp)) != EOF) {
        *is_dirty = 1;
      }
      pclose(fp);
    }
  }

  return status;
}

/**
 * Get the name of the Git repository
 */
int get_git_repo_name(char *repo_name, size_t buffer_size) {
  if (!repo_name || buffer_size == 0) {
    return 0;
  }

  repo_name[0] = '\0';

  char cmd[PATH_MAX] = "git rev-parse --show-toplevel 2>/dev/null";
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    return 0;
  }

  char path[PATH_MAX];
  if (fgets(path, sizeof(path), fp) == NULL) {
    pclose(fp);
    return 0;
  }
  pclose(fp);

  // Remove trailing newline
  char *newline = strchr(path, '\n');
  if (newline) {
    *newline = '\0';
  }

  // Extract the directory name from the full path
  char *last_slash = strrchr(path, '/');
  if (last_slash) {
    strncpy(repo_name, last_slash + 1, buffer_size - 1);
    repo_name[buffer_size - 1] = '\0';
    return 1;
  }

  return 0;
}

/**
 * Get Git status for the current repository
 */
char *get_git_status(void) {
  char branch_name[100] = "";
  int is_dirty = 0;
  char repo_name[100] = "";

  // Check if we're in a Git repo and get branch info
  if (!get_git_branch(branch_name, sizeof(branch_name), &is_dirty)) {
    return NULL; // Not in a Git repo
  }

  // Get repo name
  get_git_repo_name(repo_name, sizeof(repo_name));

  // Format status string
  char *status = malloc(256);
  if (!status) {
    return NULL;
  }

  if (strlen(repo_name) > 0) {
    sprintf(status, "%s%s (%s%s%s)", repo_name, branch_name[0] ? ":" : "",
            branch_name, is_dirty ? " *" : "", "");
  } else {
    sprintf(status, "%s%s", branch_name, is_dirty ? " *" : "");
  }

  return status;
}

int get_last_commit(char *title, size_t title_size, char *hash,
                    size_t hash_size) {
  if (!title || !hash || title_size == 0 || hash_size == 0) {
    return 0;
  }
  title[0] = '\0';
  hash[0] = '\0';

  FILE *fp = popen("git rev-parse --short HEAD 2>/dev/null", "r");
  if (fp) {
    if (fgets(hash, hash_size, fp) != NULL) {
      char *newline = strchr(hash, '\n');
      if (newline)
        *newline = '\0';
    }
    pclose(fp);
  }

  fp = popen("git log -1 --pretty=format:%s 2>/dev/null", "r");
  if (fp) {
    if (fgets(title, title_size, fp) != NULL) {
      // No need to remove quotes since we're not using them in the format.
    }
    pclose(fp);
  }

  return (strlen(hash) > 0 && strlen(title) > 0) ? 1 : 0;
}

int get_recent_commit(char commits[][256], int count) {
  if (!commits || count <= 0) {
    return 0;
  }

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "git log -%d --pretty=format:%%s 2>/dev/null",
           count);

  FILE *fp = popen(cmd, "r");
  if (!fp) {
    return 0;
  }

  int retrieved = 0;
  while (retrieved < count && fgets(commits[retrieved], 256, fp) != NULL) {
    char *newline = strchr(commits[retrieved], '\n');
    if (newline)
      *newline = '\0';

    retrieved++;
  }

  pclose(fp);
  return retrieved;
}

int get_repo_url(char *url, size_t url_size) {
  if (!url || url_size == 0) {
    return 0;
  }

  url[0] = '\0';

  FILE *fp = popen("git config --get remote.origin.url 2>/dev/null", "r");
  if (!fp) {
    return 0;
  }

  char remote_url[512];
  if (fgets(remote_url, sizeof(remote_url), fp) == NULL) {
    pclose(fp);
    return 0;
  }
  pclose(fp);

  char *newline = strchr(remote_url, '\n');
  if (newline)
    *newline = '\0';

  if (strstr(remote_url, "git@github.com:")) {
    char *repo_path = strchr(remote_url, ':');
    if (repo_path) {
      repo_path++;
      char *git_suffix = strstr(repo_path, ".git");
      if (git_suffix)
        *git_suffix = '\0';

      snprintf(url, url_size, "https://github.com/%s", repo_path);
      return 1;
    }
  } else if (strstr(remote_url, "https://github.com/")) {
    strncpy(url, remote_url, url_size - 1);
    url[url_size - 1] = '\0';
    char *git_suffix = strstr(url, ".git");
    if (git_suffix)
      *git_suffix = '\0';
    return 1;
  }

  return 0;
}

/**
 * Check if the current branch has diverged from its remote tracking branch
 */
int check_branch_divergence(int *commits_ahead, int *commits_behind) {
  if (!commits_ahead || !commits_behind) {
    return 0;
  }

  *commits_ahead = 0;
  *commits_behind = 0;

  // Check if we have a remote tracking branch
  FILE *fp = popen("git rev-parse --abbrev-ref @{u} 2>/dev/null", "r");
  if (!fp) {
    return 0;
  }

  char remote_branch[256];
  if (fgets(remote_branch, sizeof(remote_branch), fp) == NULL) {
    pclose(fp);
    return 0; // No remote tracking branch
  }
  pclose(fp);

  // Remove newline
  char *newline = strchr(remote_branch, '\n');
  if (newline) {
    *newline = '\0';
  }

  // Get commits ahead (local commits not in remote)
  fp = popen("git rev-list --count @{u}..HEAD 2>/dev/null", "r");
  if (fp) {
    char count_str[32];
    if (fgets(count_str, sizeof(count_str), fp) != NULL) {
      *commits_ahead = atoi(count_str);
    }
    pclose(fp);
  }

  // Get commits behind (remote commits not in local)
  fp = popen("git rev-list --count HEAD..@{u} 2>/dev/null", "r");
  if (fp) {
    char count_str[32];
    if (fgets(count_str, sizeof(count_str), fp) != NULL) {
      *commits_behind = atoi(count_str);
    }
    pclose(fp);
  }

  // Branch has diverged if we have commits both ahead and behind
  return (*commits_ahead > 0 && *commits_behind > 0) ? 1 : 0;
}

int create_git_stash(void) {
  FILE *fp = popen("git status --porcelain 2>/dev/null", "r");
  if (!fp)
    return 0;

  char line[256];
  int has_changes = 0;
  if (fgets(line, sizeof(line), fp) != NULL) {
    has_changes = 1;
  }
  pclose(fp);

  if (!has_changes) {
    return 0;
  }

  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);
  
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git stash push -m \"WIP: stashed at %04d-%02d-%02d %02d:%02d:%02d\" 2>/dev/null >/dev/null", 
    timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  int result = system(cmd);
  return (result == 0) ? 1 : 0;
}

int create_git_stash_with_name(const char *stash_name) {
  if (!stash_name || strlen(stash_name) == 0) {
    return 0;
  }

  FILE *fp = popen("git status --porcelain 2>/dev/null", "r");
  if (!fp)
    return 0;

  char line[256];
  int has_changes = 0;
  if (fgets(line, sizeof(line), fp) != NULL) {
    has_changes = 1;
  }
  pclose(fp);

  if (!has_changes) {
    return 0;
  }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git stash push -m \"%s\" 2>/dev/null >/dev/null", stash_name);

  int result = system(cmd);
  return (result == 0) ? 1 : 0;
}


int get_git_stashes(char stashes[][512], int max_stashes) {
	if (!stashes || max_stashes <= 0) {
		return 0;
	}

	FILE *fp = popen("git stash list --format=\"%gd: %gs\" 2>/dev/null", "r");
	if (!fp) {
		return 0;
	}

	int count = 0;
	char line[512];

	while (fgets(line, sizeof(line), fp) != NULL && count < max_stashes) {
		char *newline = strchr(line, '\n');
		if (newline) {
			*newline = '\0';
		}

		strncpy(stashes[count], line, 511);
		stashes[count][511] = '\0';
		count++;
	}

	pclose(fp);
	return count;
}
