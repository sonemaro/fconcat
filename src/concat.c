#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "concat.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define PATH_SEP '\\'
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #define PATH_SEP '/'
#endif

// Portable wildcard matching function
static int match_pattern(const char *pattern, const char *string) {
    while (*pattern && *string) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            while (*string) {
                if (match_pattern(pattern, string)) return 1;
                string++;
            }
            return 0;
        }
        if (*pattern == '?' || tolower(*pattern) == tolower(*string)) {
            pattern++;
            string++;
        } else {
            return 0;
        }
    }
    return *pattern == *string;
}

void init_exclude_list(ExcludeList *excludes) {
    excludes->count = 0;
    for (int i = 0; i < MAX_EXCLUDES; i++) {
        excludes->patterns[i] = NULL;
    }
}

static int is_verbose() {
    const char *env = getenv("FCONCAT_VERBOSE");
    return env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
}

void add_exclude_pattern(ExcludeList *excludes, const char *pattern) {
    if (excludes->count >= MAX_EXCLUDES) {
        fprintf(stderr, "Warning: Maximum number of exclude patterns (%d) reached, ignoring: %s\n", MAX_EXCLUDES, pattern);
        return;
    }
    excludes->patterns[excludes->count] = strdup(pattern);
    if (excludes->patterns[excludes->count] == NULL) {
        fprintf(stderr, "Memory allocation failed for exclude pattern: %s\n", pattern);
        exit(EXIT_FAILURE);
    }
    excludes->count++;
}

void free_exclude_list(ExcludeList *excludes) {
    for (int i = 0; i < excludes->count; i++) {
        free(excludes->patterns[i]);
        excludes->patterns[i] = NULL;
    }
    excludes->count = 0;
}

static int is_excluded(const char *path, ExcludeList *excludes) {
    for (int i = 0; i < excludes->count; i++) {
        if (match_pattern(excludes->patterns[i], path)) return 1;
    }
    return 0;
}

static int safe_path_join(char *dest, size_t dest_size, const char *path1, const char *path2) {
    if (dest_size == 0) return -1;
    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);
    size_t required_size = len1 + (len1 > 0 ? 1 : 0) + len2 + 1; // path1 + '/' + path2 + '\0'

    if (required_size > dest_size) {
        return -1; // Path would be too long
    }

    if (len1 > 0) {
        memcpy(dest, path1, len1);
        dest[len1] = PATH_SEP;
        memcpy(dest + len1 + 1, path2, len2);
        dest[len1 + 1 + len2] = '\0';
    } else {
        memcpy(dest, path2, len2);
        dest[len2] = '\0';
    }

    return 0;
}

void format_size(unsigned long long size, char *buffer, size_t buffer_size) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    int unit_index = 0;
    double size_d = (double)size;
    
    while (size_d >= 1024.0 && unit_index < 6) {
        size_d /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buffer, buffer_size, "%llu %s", size, units[unit_index]);
    } else {
        snprintf(buffer, buffer_size, "%.2f %s", size_d, units[unit_index]);
    }
}

#if defined(_WIN32) || defined(_WIN64)

unsigned long long list_files(FILE *output, const char *base_path, const char *current_path, 
                             int level, ExcludeList *excludes, int show_size, 
                             unsigned long long *total_size) {
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    char search_path[MAX_PATH];
    char full_path[MAX_PATH];
    unsigned long long dir_size = 0;

    if (safe_path_join(search_path, sizeof(search_path), base_path, current_path) < 0) {
        fprintf(stderr, "Path too long: %s\\%s\n", base_path, current_path);
        return 0;
    }
    strncat(search_path, "\\*", sizeof(search_path) - strlen(search_path) - 1);

    hFind = FindFirstFile(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open directory: %s\n", search_path);
        return 0;
    }

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        char relative_path[MAX_PATH];
        if (strlen(current_path) > 0) {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, findData.cFileName) < 0) {
                fprintf(stderr, "Path too long: %s\\%s\n", current_path, findData.cFileName);
                continue;
            }
        } else {
            strncpy(relative_path, findData.cFileName, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes)) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Excluded: %s\n", relative_path);
            continue;
        }

        for (int i = 0; i < level; i++) fprintf(output, "  ");

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            fprintf(output, "📁 %s\\\n", findData.cFileName);
            unsigned long long subdir_size = list_files(output, base_path, relative_path, level + 1, excludes, show_size, total_size);
            dir_size += subdir_size;
        } else {
            LARGE_INTEGER fileSize;
            fileSize.LowPart = findData.nFileSizeLow;
            fileSize.HighPart = findData.nFileSizeHigh;
            
            if (show_size) {
                char size_buf[32];
                format_size(fileSize.QuadPart, size_buf, sizeof(size_buf));
                fprintf(output, "📄 [%s] %s\n", size_buf, findData.cFileName);
            } else {
                fprintf(output, "📄 %s\n", findData.cFileName);
            }
            
            dir_size += fileSize.QuadPart;
            if (total_size) *total_size += fileSize.QuadPart;
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return dir_size;
}

void concat_files(FILE *output, const char *base_path, const char *current_path, ExcludeList *excludes) {
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    char search_path[MAX_PATH];
    char full_path[MAX_PATH];

    if (safe_path_join(search_path, sizeof(search_path), base_path, current_path) < 0) {
        fprintf(stderr, "Path too long: %s\\%s\n", base_path, current_path);
        return;
    }
    strncat(search_path, "\\*", sizeof(search_path) - strlen(search_path) - 1);

    hFind = FindFirstFile(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open directory: %s\n", search_path);
        return;
    }

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        char relative_path[MAX_PATH];
        if (strlen(current_path) > 0) {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, findData.cFileName) < 0) {
                fprintf(stderr, "Path too long: %s\\%s\n", current_path, findData.cFileName);
                continue;
            }
        } else {
            strncpy(relative_path, findData.cFileName, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes))
            continue;

        if (safe_path_join(full_path, sizeof(full_path), base_path, relative_path) < 0) {
            fprintf(stderr, "Path too long: %s\\%s\n", base_path, relative_path);
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            concat_files(output, base_path, relative_path, excludes);
        } else {
            FILE *input = fopen(full_path, "rb");
            if (!input) {
                if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open file: %s\n", full_path);
                continue;
            }

            fprintf(output, "// File: %s\n", relative_path);
            
            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
                if (fwrite(buffer, 1, bytes_read, output) != bytes_read) {
                    fprintf(stderr, "Error writing to output file.\n");
                    fclose(input);
                    closedir(dir);
                    return;
                }
            }
            fprintf(output, "\n\n");
            fclose(input);
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
}

#else

unsigned long long list_files(FILE *output, const char *base_path, const char *current_path, 
                             int level, ExcludeList *excludes, int show_size, 
                             unsigned long long *total_size) {
    char path[MAX_PATH];
    struct dirent *dp;
    struct stat statbuf;
    DIR *dir;
    unsigned long long dir_size = 0;

    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0) {
        fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
        return 0;
    }

    if (!(dir = opendir(path))) {
        if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open directory: %s\n", path);
        return 0;
    }

    while ((dp = readdir(dir))) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH];
        char relative_path[MAX_PATH];

        if (safe_path_join(full_path, sizeof(full_path), path, dp->d_name) < 0) {
            fprintf(stderr, "Path too long: %s/%s\n", path, dp->d_name);
            continue;
        }

        if (strlen(current_path) > 0) {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, dp->d_name) < 0) {
                fprintf(stderr, "Path too long: %s/%s\n", current_path, dp->d_name);
                continue;
            }
        } else {
            strncpy(relative_path, dp->d_name, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes)) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Excluded: %s\n", relative_path);
            continue;
        }

        if (stat(full_path, &statbuf) == -1) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Cannot stat: %s\n", full_path);
            continue;
        }

        for (int i = 0; i < level; i++) fprintf(output, "  ");

        if (S_ISDIR(statbuf.st_mode)) {
            fprintf(output, "📁 %s/\n", dp->d_name);
            unsigned long long subdir_size = list_files(output, base_path, relative_path, level + 1, excludes, show_size, total_size);
            dir_size += subdir_size;
        } else {
            if (show_size) {
                char size_buf[32];
                format_size(statbuf.st_size, size_buf, sizeof(size_buf));
                fprintf(output, "📄 [%s] %s\n", size_buf, dp->d_name);
            } else {
                fprintf(output, "📄 %s\n", dp->d_name);
            }
            
            dir_size += statbuf.st_size;
            if (total_size) *total_size += statbuf.st_size;
        }
    }

    closedir(dir);
    return dir_size;
}

void concat_files(FILE *output, const char *base_path, const char *current_path, ExcludeList *excludes) {
    char path[MAX_PATH];
    struct dirent *dp;
    struct stat statbuf;
    DIR *dir;

    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0) {
        fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
        return;
    }

    if (!(dir = opendir(path))) {
        if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open directory: %s\n", path);
        return;
    }

    while ((dp = readdir(dir))) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH];
        char relative_path[MAX_PATH];

        if (safe_path_join(full_path, sizeof(full_path), path, dp->d_name) < 0) {
            fprintf(stderr, "Path too long: %s/%s\n", path, dp->d_name);
            continue;
        }

        if (strlen(current_path) > 0) {
            if (safe_path_join(relative_path, sizeof(relative_path), current_path, dp->d_name) < 0) {
                fprintf(stderr, "Path too long: %s/%s\n", current_path, dp->d_name);
                continue;
            }
        } else {
            strncpy(relative_path, dp->d_name, sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
        }

        if (is_excluded(relative_path, excludes)) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Excluded: %s\n", relative_path);
            continue;
        }

        if (stat(full_path, &statbuf) == -1) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Cannot stat: %s\n", full_path);
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            concat_files(output, base_path, relative_path, excludes);
        } else {
            FILE *input = fopen(full_path, "rb");
            if (!input) {
                if (is_verbose()) fprintf(stderr, "[fconcat] Cannot open file: %s\n", full_path);
                continue;
            }

            fprintf(output, "// File: %s\n", relative_path);

            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
                if (fwrite(buffer, 1, bytes_read, output) != bytes_read) {
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