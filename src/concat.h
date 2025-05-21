#ifndef CONCAT_H
#define CONCAT_H

#include <stdio.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096  // Default value if PATH_MAX is not defined
#endif

#define MAX_PATH 4096
#define BUFFER_SIZE 4096
#define MAX_EXCLUDES 100

#if defined(_WIN32) || defined(_WIN64)
    #define PATH_SEP '\\'
#else
    #define PATH_SEP '/'
#endif

typedef struct {
    char *patterns[MAX_EXCLUDES];
    int count;
} ExcludeList;

/**
 * Recursively list files and directories, writing to output.
 * @param output Output file pointer.
 * @param base_path Root directory.
 * @param current_path Relative path from base_path.
 * @param level Indentation level.
 * @param excludes List of exclude patterns.
 * @param show_size Flag to show file sizes.
 * @param total_size Pointer to store total size (can be NULL if not tracking).
 * @return The total size of files listed.
 */
unsigned long long list_files(FILE *output, const char *base_path, const char *current_path, 
                             int level, ExcludeList *excludes, int show_size, 
                             unsigned long long *total_size);

/**
 * Recursively concatenate files, writing their contents to output.
 * @param output Output file pointer.
 * @param base_path Root directory.
 * @param current_path Relative path from base_path.
 * @param excludes List of exclude patterns.
 */
void concat_files(FILE *output, const char *base_path, const char *current_path, ExcludeList *excludes);

/**
 * Initialize an ExcludeList.
 */
void init_exclude_list(ExcludeList *excludes);

/**
 * Add a pattern to the ExcludeList.
 */
void add_exclude_pattern(ExcludeList *excludes, const char *pattern);

/**
 * Free memory used by ExcludeList.
 */
void free_exclude_list(ExcludeList *excludes);

/**
 * Format file size to human-readable format (KB, MB, GB, etc.)
 * @param size Size in bytes
 * @param buffer Buffer to store formatted string
 * @param buffer_size Size of buffer
 */
void format_size(unsigned long long size, char *buffer, size_t buffer_size);

#endif

