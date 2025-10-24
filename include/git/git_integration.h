
#ifndef GIT_INTEGRATION_H
#define GIT_INTEGRATION_H

#include "common.h"

void init_git_integration(void);

int get_git_branch(char *branch_name, size_t buffer_size, int *is_dirty);

int get_last_commit(char *title, size_t title_size, char *hash,
                    size_t hash_size);

int get_recent_commit(char commits[][256], int count);

int get_repo_url(char *url, size_t url_size);

int get_git_repo_name(char *repo_name, size_t buffer_size);

char *get_git_status(void);

int check_branch_divergence(int *commits_ahead, int *commits_behind);

int create_git_stash(void);

int create_git_stash_with_name(const char *stash_name);

int get_git_stashes(char stashed[][512], int max_stashes);

int apply_git_stash(int stash_index);

int pop_git_stash(int stash_index);

int drop_git_stash(int stash_index);

int get_commit_details(const char *commit_hash, char *commit_info, size_t info_size);

int get_stash_diff(int stash_index, char *stash_diff, size_t diff_size);

int get_branch_commits(const char *branch_name, char commits[][2048], int max_commits);

#endif // GIT_INTEGRATION_H
