#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "concat.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wchar.h>
#define PATH_SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

// Simple hash function for exclude patterns
static unsigned int hash_string(const char *str)
{
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % MAX_EXCLUDES;
}

// Portable wildcard matching function
static int match_pattern(const char *pattern, const char *string)
{
    while (*pattern && *string)
    {
        if (*pattern == '*')
        {
            pattern++;
            if (!*pattern)
                return 1;
            while (*string)
            {
                if (match_pattern(pattern, string))
                    return 1;
                string++;
            }
            return 0;
        }
        if (*pattern == '?' || tolower(*pattern) == tolower(*string))
        {
            pattern++;
            string++;
        }
        else
        {
            return 0;
        }
    }
    return *pattern == *string;
}

void init_exclude_list(ExcludeList *excludes)
{
    excludes->count = 0;
    for (int i = 0; i < MAX_EXCLUDES; i++)
    {
        excludes->buckets[i] = NULL;
    }
}

static int is_verbose()
{
    const char *env = getenv("FCONCAT_VERBOSE");
    return env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
}

void add_exclude_pattern(ExcludeList *excludes, const char *pattern)
{
    if (!pattern || strlen(pattern) == 0)
        return;

    unsigned int bucket = hash_string(pattern);

    // Check if pattern already exists in this bucket
    ExcludeNode *current = excludes->buckets[bucket];
    while (current)
    {
        if (strcmp(current->pattern, pattern) == 0)
        {
            return; // Pattern already exists
        }
        current = current->next;
    }

    // Add new pattern
    ExcludeNode *new_node = malloc(sizeof(ExcludeNode));
    if (!new_node)
    {
        fprintf(stderr, "Memory allocation failed for exclude pattern: %s\n", pattern);
        exit(EXIT_FAILURE);
    }

    new_node->pattern = strdup(pattern);
    if (!new_node->pattern)
    {
        fprintf(stderr, "Memory allocation failed for exclude pattern: %s\n", pattern);
        free(new_node);
        exit(EXIT_FAILURE);
    }

    new_node->next = excludes->buckets[bucket];
    excludes->buckets[bucket] = new_node;
    excludes->count++;
}

void free_exclude_list(ExcludeList *excludes)
{
    for (int i = 0; i < MAX_EXCLUDES; i++)
    {
        ExcludeNode *current = excludes->buckets[i];
        while (current)
        {
            ExcludeNode *next = current->next;
            free(current->pattern);
            free(current);
            current = next;
        }
        excludes->buckets[i] = NULL;
    }
    excludes->count = 0;
}

static int is_excluded(const char *path, ExcludeList *excludes)
{
    // Check full path match
    for (int i = 0; i < MAX_EXCLUDES; i++)
    {
        ExcludeNode *current = excludes->buckets[i];
        while (current)
        {
            if (match_pattern(current->pattern, path))
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Excluded (full path match): %s\n", path);
                return 1;
            }
            current = current->next;
        }
    }

    // Check basename match
    const char *basename = strrchr(path, PATH_SEP);
    if (basename)
    {
        basename++; // Skip the separator
        for (int i = 0; i < MAX_EXCLUDES; i++)
        {
            ExcludeNode *current = excludes->buckets[i];
            while (current)
            {
                if (match_pattern(current->pattern, basename))
                {
                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Excluded (basename match): %s\n", path);
                    return 1;
                }
                current = current->next;
            }
        }
    }

    return 0;
}

static int safe_path_join(char *dest, size_t dest_size, const char *path1, const char *path2)
{
    if (dest_size == 0)
        return -1;
    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);
    size_t required_size = len1 + (len1 > 0 ? 1 : 0) + len2 + 1; // path1 + '/' + path2 + '\0'

    if (required_size > dest_size)
    {
        return -1; // Path would be too long
    }

    if (len1 > 0)
    {
        memcpy(dest, path1, len1);
        dest[len1] = PATH_SEP;
        memcpy(dest + len1 + 1, path2, len2);
        dest[len1 + 1 + len2] = '\0';
    }
    else
    {
        memcpy(dest, path2, len2);
        dest[len2] = '\0';
    }

    return 0;
}

void format_size(unsigned long long size, char *buffer, size_t buffer_size)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    int unit_index = 0;
    double size_d = (double)size;

    while (size_d >= 1024.0 && unit_index < 6)
    {
        size_d /= 1024.0;
        unit_index++;
    }

    if (unit_index == 0)
    {
        snprintf(buffer, buffer_size, "%llu %s", size, units[unit_index]);
    }
    else
    {
        snprintf(buffer, buffer_size, "%.2f %s", size_d, units[unit_index]);
    }
}

int is_binary_file(const char *filepath)
{
    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        return -1; // Error opening file
    }

    unsigned char buffer[BINARY_CHECK_SIZE];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);

    if (bytes_read == 0)
    {
        return 0; // Empty file, treat as text
    }

    // Check for null bytes and other binary indicators
    size_t null_count = 0;
    size_t control_count = 0;
    size_t high_bit_count = 0;

    for (size_t i = 0; i < bytes_read; i++)
    {
        unsigned char byte = buffer[i];

        if (byte == 0)
        {
            null_count++;
        }
        else if (byte < 32 && byte != '\t' && byte != '\n' && byte != '\r')
        {
            control_count++;
        }
        else if (byte > 127)
        {
            high_bit_count++;
        }
    }

    // Heuristics for binary detection
    if (null_count > 0)
        return 1; // Any null bytes = binary
    if (control_count > bytes_read / 20)
        return 1; // >5% control chars = binary
    if (high_bit_count > bytes_read / 2)
        return 1; // >50% high-bit chars = binary

    return 0; // Likely text
}

#if defined(_WIN32) || defined(_WIN64)

wchar_t *utf8_to_wide(const char *utf8_path)
{
    if (!utf8_path)
        return NULL;

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (wide_len == 0)
        return NULL;

    wchar_t *wide_path = malloc(wide_len * sizeof(wchar_t));
    if (!wide_path)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide_path, wide_len) == 0)
    {
        free(wide_path);
        return NULL;
    }

    return wide_path;
}

char *wide_to_utf8(const wchar_t *wide_path)
{
    if (!wide_path)
        return NULL;

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, NULL, 0, NULL, NULL);
    if (utf8_len == 0)
        return NULL;

    char *utf8_path = malloc(utf8_len);
    if (!utf8_path)
        return NULL;

    if (WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, utf8_path, utf8_len, NULL, NULL) == 0)
    {
        free(utf8_path);
        return NULL;
    }

    return utf8_path;
}

unsigned long long list_files(FILE *output, const char *base_path, const char *current_path,
                              int level, ExcludeList *excludes, int show_size,
                              unsigned long long *total_size)
{
    WIN32_FIND_DATAW findData;
    HANDLE hFind;
    char search_path[MAX_PATH];
    unsigned long long dir_size = 0;

    if (safe_path_join(search_path, sizeof(search_path), base_path, current_path) < 0)
    {
        fprintf(stderr, "Path too long: %s\\%s\n", base_path, current_path);
        return 0;
    }

    // Convert to wide string and add search pattern
    wchar_t *wide_search = utf8_to_wide(search_path);
    if (!wide_search)
    {
        fprintf(stderr, "Failed to convert path to wide string: %s\n", search_path);
        return 0;
    }

    wchar_t wide_search_pattern[MAX_PATH];
    swprintf(wide_search_pattern, MAX_PATH, L"%ls\\*", wide_search);
    free(wide_search);

    hFind = FindFirstFileW(wide_search_pattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Cannot open directory: %s\n", search_path);
        return 0;
    }

    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        char *utf8_filename = wide_to_utf8(findData.cFileName);
        if (!utf8_filename)
            continue;

        char relative_path[MAX_PATH];
        if (strlen(current_path) > 0)
        {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, utf8_filename) < 0)
            {
                fprintf(stderr, "Path too long: %s\\%s\n", current_path, utf8_filename);
                free(utf8_filename);
                continue;
            }
        }
        else
        {
            strncpy(relative_path, utf8_filename, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes))
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Excluded: %s\n", relative_path);
            free(utf8_filename);
            continue;
        }

        for (int i = 0; i < level; i++)
            fprintf(output, "  ");

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            fprintf(output, "📁 %s\\\n", utf8_filename);
            unsigned long long subdir_size = list_files(output, base_path, relative_path, level + 1, excludes, show_size, total_size);
            dir_size += subdir_size;
        }
        else
        {
            LARGE_INTEGER fileSize;
            fileSize.LowPart = findData.nFileSizeLow;
            fileSize.HighPart = findData.nFileSizeHigh;

            if (show_size)
            {
                char size_buf[32];
                format_size(fileSize.QuadPart, size_buf, sizeof(size_buf));
                fprintf(output, "📄 [%s] %s\n", size_buf, utf8_filename);
            }
            else
            {
                fprintf(output, "📄 %s\n", utf8_filename);
            }

            dir_size += fileSize.QuadPart;
            if (total_size)
                *total_size += fileSize.QuadPart;
        }

        free(utf8_filename);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return dir_size;
}

void concat_files(FILE *output, const char *base_path, const char *current_path,
                  ExcludeList *excludes, BinaryHandling binary_handling)
{
    WIN32_FIND_DATAW findData;
    HANDLE hFind;
    char search_path[MAX_PATH];

    if (safe_path_join(search_path, sizeof(search_path), base_path, current_path) < 0)
    {
        fprintf(stderr, "Path too long: %s\\%s\n", base_path, current_path);
        return;
    }

    wchar_t *wide_search = utf8_to_wide(search_path);
    if (!wide_search)
    {
        fprintf(stderr, "Failed to convert path to wide string: %s\n", search_path);
        return;
    }

    wchar_t wide_search_pattern[MAX_PATH];
    swprintf(wide_search_pattern, MAX_PATH, L"%ls\\*", wide_search);
    free(wide_search);

    hFind = FindFirstFileW(wide_search_pattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Cannot open directory: %s\n", search_path);
        return;
    }

    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        char *utf8_filename = wide_to_utf8(findData.cFileName);
        if (!utf8_filename)
            continue;

        char relative_path[MAX_PATH];
        char full_path[MAX_PATH];

        if (strlen(current_path) > 0)
        {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, utf8_filename) < 0)
            {
                fprintf(stderr, "Path too long: %s\\%s\n", current_path, utf8_filename);
                free(utf8_filename);
                continue;
            }
        }
        else
        {
            strncpy(relative_path, utf8_filename, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes))
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Excluded from concat: %s\n", relative_path);
            free(utf8_filename);
            continue;
        }

        if (safe_path_join(full_path, sizeof(full_path), base_path, relative_path) < 0)
        {
            fprintf(stderr, "Path too long: %s\\%s\n", base_path, relative_path);
            free(utf8_filename);
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            concat_files(output, base_path, relative_path, excludes, binary_handling);
        }
        else
        {
            // Check if file is binary
            int is_binary = is_binary_file(full_path);
            if (is_binary == 1)
            {
                if (binary_handling == BINARY_SKIP)
                {
                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Skipping binary file: %s\n", relative_path);
                    free(utf8_filename);
                    continue;
                }
                else if (binary_handling == BINARY_PLACEHOLDER)
                {
                    fprintf(output, "// File: %s\n", relative_path);
                    fprintf(output, "// [Binary file - content not displayed]\n\n");
                    free(utf8_filename);
                    continue;
                }
            }

            FILE *input = fopen(full_path, "rb");
            if (!input)
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Cannot open file: %s\n", full_path);
                free(utf8_filename);
                continue;
            }

            fprintf(output, "// File: %s\n", relative_path);

            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0)
            {
                if (fwrite(buffer, 1, bytes_read, output) != bytes_read)
                {
                    fprintf(stderr, "Error writing to output file.\n");
                    fclose(input);
                    free(utf8_filename);
                    FindClose(hFind);
                    return;
                }
            }
            fprintf(output, "\n\n");
            fclose(input);
        }

        free(utf8_filename);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

#else

unsigned long long list_files(FILE *output, const char *base_path, const char *current_path,
                              int level, ExcludeList *excludes, int show_size,
                              unsigned long long *total_size)
{
    char path[MAX_PATH];
    struct dirent *dp;
    struct stat statbuf;
    DIR *dir;
    unsigned long long dir_size = 0;

    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0)
    {
        fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
        return 0;
    }

    if (!(dir = opendir(path)))
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Cannot open directory: %s\n", path);
        return 0;
    }

    while ((dp = readdir(dir)))
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH];
        char relative_path[MAX_PATH];

        if (safe_path_join(full_path, sizeof(full_path), path, dp->d_name) < 0)
        {
            fprintf(stderr, "Path too long: %s/%s\n", path, dp->d_name);
            continue;
        }

        if (strlen(current_path) > 0)
        {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, dp->d_name) < 0)
            {
                fprintf(stderr, "Path too long: %s/%s\n", current_path, dp->d_name);
                continue;
            }
        }
        else
        {
            strncpy(relative_path, dp->d_name, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes))
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Excluded: %s\n", relative_path);
            continue;
        }

        if (stat(full_path, &statbuf) == -1)
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Cannot stat: %s\n", full_path);
            continue;
        }

        for (int i = 0; i < level; i++)
            fprintf(output, "  ");

        if (S_ISDIR(statbuf.st_mode))
        {
            fprintf(output, "📁 %s/\n", dp->d_name);
            unsigned long long subdir_size = list_files(output, base_path, relative_path, level + 1, excludes, show_size, total_size);
            dir_size += subdir_size;
        }
        else
        {
            if (show_size)
            {
                char size_buf[32];
                format_size(statbuf.st_size, size_buf, sizeof(size_buf));
                fprintf(output, "📄 [%s] %s\n", size_buf, dp->d_name);
            }
            else
            {
                fprintf(output, "📄 %s\n", dp->d_name);
            }

            dir_size += statbuf.st_size;
            if (total_size)
                *total_size += statbuf.st_size;
        }
    }

    closedir(dir);
    return dir_size;
}

void concat_files(FILE *output, const char *base_path, const char *current_path,
                  ExcludeList *excludes, BinaryHandling binary_handling)
{
    char path[MAX_PATH];
    struct dirent *dp;
    struct stat statbuf;
    DIR *dir;

    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0)
    {
        fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
        return;
    }

    if (!(dir = opendir(path)))
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Cannot open directory: %s\n", path);
        return;
    }

    while ((dp = readdir(dir)))
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH];
        char relative_path[MAX_PATH];

        if (safe_path_join(full_path, sizeof(full_path), path, dp->d_name) < 0)
        {
            fprintf(stderr, "Path too long: %s/%s\n", path, dp->d_name);
            continue;
        }

        if (strlen(current_path) > 0)
        {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, dp->d_name) < 0)
            {
                fprintf(stderr, "Path too long: %s/%s\n", current_path, dp->d_name);
                continue;
            }
        }
        else
        {
            strncpy(relative_path, dp->d_name, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes))
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Excluded from concat: %s\n", relative_path);
            continue;
        }

        if (stat(full_path, &statbuf) == -1)
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Cannot stat: %s\n", full_path);
            continue;
        }

        if (S_ISDIR(statbuf.st_mode))
        {
            concat_files(output, base_path, relative_path, excludes, binary_handling);
        }
        else
        {
            // Check if file is binary
            int is_binary = is_binary_file(full_path);
            if (is_binary == 1)
            {
                if (binary_handling == BINARY_SKIP)
                {
                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Skipping binary file: %s\n", relative_path);
                    continue;
                }
                else if (binary_handling == BINARY_PLACEHOLDER)
                {
                    fprintf(output, "// File: %s\n", relative_path);
                    fprintf(output, "// [Binary file - content not displayed]\n\n");
                    continue;
                }
            }

            FILE *input = fopen(full_path, "rb");
            if (!input)
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Cannot open file: %s\n", full_path);
                continue;
            }

            fprintf(output, "// File: %s\n", relative_path);

            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0)
            {
                if (fwrite(buffer, 1, bytes_read, output) != bytes_read)
                {
                    fprintf(stderr, "Error writing to output file.\n");
                    fclose(input);
                    closedir(dir);
                    return;
                }
            }
            fprintf(output, "\n\n");
            fclose(input);
        }
    }

    closedir(dir);
}

#endif