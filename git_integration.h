/**
 * git_integration.h
 * Functions for Git repository detection and information
 */

#ifndef GIT_INTEGRATION_H
#define GIT_INTEGRATION_H

#include "common.h"

/**
 * Initialize Git integration
 */
void init_git_integration(void);

/**
 * Check if the current directory is in a Git repository and get branch info
 *
 * @param branch_name Buffer to store branch name if found
 * @param buffer_size Size of the branch_name buffer
 * @param is_dirty Pointer to store dirty status flag (1 if repo has changes)
 * @return 1 if in a Git repo, 0 otherwise
 */
int get_git_branch(char *branch_name, size_t buffer_size, int *is_dirty);

int get_last_commit(char *title, size_t title_size, char *hash,
                    size_t hash_size);

int get_recent_commit(char commits[][256], int count);

int get_repo_url(char *url, size_t url_size);

/**
 * Get the name of the Git repository
 *
 * @param repo_name Buffer to store repo name if found
 * @param buffer_size Size of the repo_name buffer
 * @return 1 if name was retrieved, 0 otherwise
 */
int get_git_repo_name(char *repo_name, size_t buffer_size);

/**
 * Get Git status for the current repository
 *
 * @return A string with the Git status or NULL if not in a repo
 *         (caller must free the returned string)
 */
char *get_git_status(void);

/**
 * Check if the current branch has diverged from its remote tracking branch
 *
 * @param commits_ahead Pointer to store number of commits ahead of remote
 * @param commits_behind Pointer to store number of commits behind remote  
 * @return 1 if diverged (both ahead and behind > 0), 0 otherwise
 */
int check_branch_divergence(int *commits_ahead, int *commits_behind);

#endif // GIT_INTEGRATION_H
