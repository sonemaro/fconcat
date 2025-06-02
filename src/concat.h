#ifndef CONCAT_H
#define CONCAT_H

#include <stdio.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096 // Default value if PATH_MAX is not defined
#endif

#define MAX_PATH 4096
#define BUFFER_SIZE 4096
#define MAX_EXCLUDES 1000      // Increased for better pattern support
#define BINARY_CHECK_SIZE 8192 // Bytes to check for binary detection

#if defined(_WIN32) || defined(_WIN64)
#define PATH_SEP '\\'
#include <windows.h>
#include <wchar.h>
#else
#define PATH_SEP '/'
#endif

// Hash table for efficient exclude pattern storage
typedef struct ExcludeNode
{
    char *pattern;
    struct ExcludeNode *next;
} ExcludeNode;

typedef struct
{
    ExcludeNode *buckets[MAX_EXCLUDES];
    int count;
} ExcludeList;

typedef enum
{
    BINARY_SKIP,
    BINARY_INCLUDE,
    BINARY_PLACEHOLDER
} BinaryHandling;

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
 * @param binary_handling How to handle binary files.
 */
void concat_files(FILE *output, const char *base_path, const char *current_path,
                  ExcludeList *excludes, BinaryHandling binary_handling);

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

/**
 * Check if a file appears to be binary.
 * @param filepath Path to the file to check.
 * @return 1 if binary, 0 if text, -1 on error.
 */
int is_binary_file(const char *filepath);

/**
 * Convert UTF-8 string to platform-appropriate format for file operations.
 * On Windows, converts to wide characters. On Unix, returns original string.
 * @param utf8_path UTF-8 encoded path
 * @return Platform-appropriate path (caller must free on Windows)
 */
#if defined(_WIN32) || defined(_WIN64)
wchar_t *utf8_to_wide(const char *utf8_path);
char *wide_to_utf8(const wchar_t *wide_path);
#else
#define utf8_to_wide(x) (x)
#define wide_to_utf8(x) (x)
#endif

#endif