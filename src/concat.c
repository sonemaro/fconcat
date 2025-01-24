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

void add_exclude_pattern(ExcludeList *excludes, const char *pattern) {
    if (excludes->count >= MAX_EXCLUDES) return;
    excludes->patterns[excludes->count] = strdup(pattern);
    excludes->count++;
}

void free_exclude_list(ExcludeList *excludes) {
    for (int i = 0; i < excludes->count; i++) {
        free(excludes->patterns[i]);
    }
}

static int is_excluded(const char *path, ExcludeList *excludes) {
    for (int i = 0; i < excludes->count; i++) {
        if (match_pattern(excludes->patterns[i], path)) return 1;
    }
    return 0;
}

static int safe_path_join(char *dest, size_t dest_size, const char *path1, const char *path2) {
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

#if defined(_WIN32) || defined(_WIN64)

void list_files(FILE *output, const char *base_path, const char *current_path, int level, ExcludeList *excludes) {
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
    if (hFind == INVALID_HANDLE_VALUE) return;

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

        for (int i = 0; i < level; i++) fprintf(output, "  ");

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            fprintf(output, "📁 %s\\\n", findData.cFileName);
            list_files(output, base_path, relative_path, level + 1, excludes);
        } else {
            fprintf(output, "📄 %s\n", findData.cFileName);
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
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
    if (hFind == INVALID_HANDLE_VALUE) return;

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
            if (!input) continue;

            fprintf(output, "// File: %s\n", relative_path);
            
            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
                fwrite(buffer, 1, bytes_read, output);
            }
            fprintf(output, "\n\n");
            fclose(input);
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
}

#else

void list_files(FILE *output, const char *base_path, const char *current_path, int level, ExcludeList *excludes) {
    char path[MAX_PATH];
    struct dirent *dp;
    struct stat statbuf;
    DIR *dir;

    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0) {
        fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
        return;
    }

    if (!(dir = opendir(path))) {
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

        if (is_excluded(relative_path, excludes))
            continue;

        if (stat(full_path, &statbuf) == -1)
            continue;

        for (int i = 0; i < level; i++) fprintf(output, "  ");

        if (S_ISDIR(statbuf.st_mode)) {
            fprintf(output, "📁 %s/\n", dp->d_name);
            list_files(output, base_path, relative_path, level + 1, excludes);
        } else {
            fprintf(output, "📄 %s\n", dp->d_name);
        }
    }

    closedir(dir);
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

        if (is_excluded(relative_path, excludes))
            continue;

        if (stat(full_path, &statbuf) == -1)
            continue;

        if (S_ISDIR(statbuf.st_mode)) {
            concat_files(output, base_path, relative_path, excludes);
        } else {
            FILE *input = fopen(full_path, "rb");
            if (!input) continue;

            fprintf(output, "// File: %s\n", relative_path);
            
            char buffer[BUFFER_SIZE];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), input)) > 0) {
                fwrite(buffer, 1, bytes_read, output);
            }
            fprintf(output, "\n\n");
            fclose(input);
        }
    }

    closedir(dir);
}

#endif
