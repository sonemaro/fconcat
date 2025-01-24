#ifndef CONCAT_H
#define CONCAT_H

#define MAX_PATH 4096
#define BUFFER_SIZE 4096
#define MAX_EXCLUDES 100

typedef struct {
    char *patterns[MAX_EXCLUDES];
    int count;
} ExcludeList;

void list_files(FILE *output, const char *base_path, const char *current_path, int level, ExcludeList *excludes);
void concat_files(FILE *output, const char *base_path, const char *current_path, ExcludeList *excludes);
void init_exclude_list(ExcludeList *excludes);
void add_exclude_pattern(ExcludeList *excludes, const char *pattern);
void free_exclude_list(ExcludeList *excludes);

#endif

