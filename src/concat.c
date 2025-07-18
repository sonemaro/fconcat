// File: src/concat.c
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include "concat.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wchar.h>
#define PATH_SEP '\\'
#ifdef WITH_PLUGINS
#define dlopen(lib, flags) LoadLibraryA(lib)
#define dlsym(handle, symbol) GetProcAddress((HMODULE)(handle), symbol)
#define dlclose(handle) FreeLibrary((HMODULE)(handle))

// Better Windows error reporting
static char win_error_buffer[256];
static const char *win_dlerror(void)
{
    DWORD error = GetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, win_error_buffer, sizeof(win_error_buffer), NULL);
    return win_error_buffer;
}
#define dlerror() win_dlerror()
#endif
#else
#define PATH_SEP '/'
#endif

#if !defined(_WIN32) && !defined(_WIN64)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef WITH_PLUGINS
#include <dlfcn.h>
#endif
#endif

// Global verbose flag
static int g_verbose = 0;

static void init_verbose()
{
    static int initialized = 0;
    if (!initialized)
    {
        const char *env = getenv("FCONCAT_VERBOSE");
        g_verbose = env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
        initialized = 1;
    }
}

static int is_verbose()
{
    init_verbose();
    return g_verbose;
}

#ifdef WITH_PLUGINS
// Plugin system implementation
int init_plugin_manager(PluginManager *manager)
{
    manager->count = 0;
    manager->initialized = 0;
    for (int i = 0; i < MAX_PLUGINS; i++)
    {
        manager->plugins[i] = NULL;
    }

    if (pthread_mutex_init(&manager->mutex, NULL) != 0)
    {
        return -1;
    }

    manager->initialized = 1;
    return 0;
}

void destroy_plugin_manager(PluginManager *manager)
{
    if (!manager->initialized)
        return;

    pthread_mutex_lock(&manager->mutex);

    // Cleanup all plugins
    for (int i = 0; i < manager->count; i++)
    {
        if (manager->plugins[i])
        {
            if (manager->plugins[i]->cleanup)
            {
                manager->plugins[i]->cleanup();
            }

            if (manager->plugins[i]->handle)
            {
                dlclose(manager->plugins[i]->handle);
            }

            free(manager->plugins[i]);
            manager->plugins[i] = NULL;
        }
    }

    manager->count = 0;
    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);
    manager->initialized = 0;
}

int load_plugin(PluginManager *manager, const char *plugin_path)
{
    if (!manager->initialized)
        return -1;

    if (manager->count >= MAX_PLUGINS)
    {
        fprintf(stderr, "Maximum number of plugins (%d) reached\n", MAX_PLUGINS);
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    // Load the plugin library
    void *handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle)
    {
        fprintf(stderr, "Cannot load plugin %s: %s\n", plugin_path, dlerror());
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    // Get the plugin interface
    void *func_ptr = dlsym(handle, "get_plugin");
    StreamingPlugin *(*get_plugin)(void) = (StreamingPlugin * (*)(void)) func_ptr;
    if (!get_plugin)
    {
        fprintf(stderr, "Cannot find get_plugin function in %s: %s\n", plugin_path, dlerror());
        dlclose(handle);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    StreamingPlugin *plugin_template = get_plugin();
    if (!plugin_template)
    {
        fprintf(stderr, "get_plugin returned NULL for %s\n", plugin_path);
        dlclose(handle);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    // Create a copy of the plugin structure
    StreamingPlugin *plugin = malloc(sizeof(StreamingPlugin));
    if (!plugin)
    {
        fprintf(stderr, "Memory allocation failed for plugin %s\n", plugin_path);
        dlclose(handle);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    *plugin = *plugin_template;
    plugin->handle = handle;
    plugin->index = manager->count;

    // Initialize the plugin
    if (plugin->init && plugin->init() != 0)
    {
        fprintf(stderr, "Plugin initialization failed for %s\n", plugin_path);
        free(plugin);
        dlclose(handle);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }

    manager->plugins[manager->count] = plugin;
    manager->count++;

    printf("✅ Loaded plugin: %s v%s\n", plugin->name, plugin->version);

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}

int process_file_through_plugins(PluginManager *manager, const char *relative_path,
                                 const char *input_data, size_t input_size,
                                 char **output_data, size_t *output_size)
{
    if (!manager || !manager->initialized || manager->count == 0)
    {
        // No plugins, pass through unchanged
        *output_data = malloc(input_size);
        if (!*output_data)
            return -1;
        memcpy(*output_data, input_data, input_size);
        *output_size = input_size;
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    // Create local plugin contexts for this processing session
    PluginContext *local_contexts[MAX_PLUGINS];
    for (int i = 0; i < MAX_PLUGINS; i++)
    {
        local_contexts[i] = NULL;
    }

    // Start file processing for all plugins
    for (int i = 0; i < manager->count; i++)
    {
        if (manager->plugins[i] && manager->plugins[i]->file_start)
        {
            local_contexts[i] = manager->plugins[i]->file_start(relative_path);
        }
    }

    // Create a copy of input data to work with
    char *current_input = malloc(input_size);
    if (!current_input)
    {
        // Cleanup contexts
        for (int i = 0; i < manager->count; i++)
        {
            if (local_contexts[i] && manager->plugins[i] && manager->plugins[i]->file_cleanup)
            {
                manager->plugins[i]->file_cleanup(local_contexts[i]);
            }
        }
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }
    memcpy(current_input, input_data, input_size);
    size_t current_size = input_size;

    // Process through each plugin in sequence
    for (int i = 0; i < manager->count; i++)
    {
        if (manager->plugins[i] && manager->plugins[i]->process_chunk && local_contexts[i])
        {
            char *plugin_output = NULL;
            size_t plugin_output_size = 0;

            int result = manager->plugins[i]->process_chunk(local_contexts[i],
                                                            current_input, current_size,
                                                            &plugin_output, &plugin_output_size);

            if (result != 0)
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Plugin %s failed processing chunk\n", manager->plugins[i]->name);
                // Skip this plugin but continue with others
                if (plugin_output)
                {
                    free(plugin_output);
                }
                continue;
            }

            // If plugin produced output, use it as input for next plugin
            if (plugin_output && plugin_output_size > 0)
            {
                // Free previous input
                free(current_input);

                // Use plugin output as new input
                current_input = plugin_output;
                current_size = plugin_output_size;

                // Update plugin context
                local_contexts[i]->total_processed += current_size;
            }
            else
            {
                // Plugin didn't produce output, continue with current input
                if (plugin_output)
                {
                    free(plugin_output);
                }
            }
        }
    }

    // End file processing for all plugins
    for (int i = 0; i < manager->count; i++)
    {
        if (manager->plugins[i] && local_contexts[i])
        {
            if (manager->plugins[i]->file_end)
            {
                char *final_output = NULL;
                size_t final_size = 0;
                int result = manager->plugins[i]->file_end(local_contexts[i], &final_output, &final_size);
                if (result == 0 && final_output && final_size > 0)
                {
                    // For now, we'll ignore final output from file_end
                    // In a more sophisticated implementation, we'd append it
                    free(final_output);
                }
            }

            if (manager->plugins[i]->file_cleanup)
            {
                manager->plugins[i]->file_cleanup(local_contexts[i]);
            }
        }
    }

    // Set final output
    *output_data = current_input;
    *output_size = current_size;

    pthread_mutex_unlock(&manager->mutex);
    return 0;
}
#endif // WITH_PLUGINS

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
    // Create normalized copies for comparison on Windows
#ifdef _WIN32
    char norm_pattern[1024];
    char norm_string[1024];

    // Normalize pattern
    size_t i = 0;
    for (const char *p = pattern; *p && i < sizeof(norm_pattern) - 1; p++, i++)
    {
        norm_pattern[i] = (*p == '\\') ? '/' : tolower(*p);
    }
    norm_pattern[i] = '\0';

    // Normalize string
    i = 0;
    for (const char *s = string; *s && i < sizeof(norm_string) - 1; s++, i++)
    {
        norm_string[i] = (*s == '\\') ? '/' : tolower(*s);
    }
    norm_string[i] = '\0';

    // Use normalized versions for matching
    const char *pat = norm_pattern;
    const char *str = norm_string;
#else
    const char *pat = pattern;
    const char *str = string;
#endif

    while (*pat && *str)
    {
        if (*pat == '*')
        {
            pat++;
            if (!*pat)
                return 1;
            while (*str)
            {
                if (match_pattern(pat, str))
                    return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?' || *pat == *str)
        {
            pat++;
            str++;
        }
        else
        {
            return 0;
        }
    }
    return *pat == *str;
}

void init_exclude_list(ExcludeList *excludes)
{
    excludes->count = 0;
    for (int i = 0; i < MAX_EXCLUDES; i++)
    {
        excludes->buckets[i] = NULL;
    }
    pthread_mutex_init(&excludes->mutex, NULL);
}

void add_exclude_pattern(ExcludeList *excludes, const char *pattern)
{
    if (!pattern || strlen(pattern) == 0)
        return;

    pthread_mutex_lock(&excludes->mutex);

    unsigned int bucket = hash_string(pattern);

    // Check if pattern already exists in this bucket
    ExcludeNode *current = excludes->buckets[bucket];
    while (current)
    {
        if (strcmp(current->pattern, pattern) == 0)
        {
            pthread_mutex_unlock(&excludes->mutex);
            return; // Pattern already exists
        }
        current = current->next;
    }

    // Add new pattern
    ExcludeNode *new_node = malloc(sizeof(ExcludeNode));
    if (!new_node)
    {
        fprintf(stderr, "Memory allocation failed for exclude pattern: %s\n", pattern);
        pthread_mutex_unlock(&excludes->mutex);
        return;
    }

    new_node->pattern = strdup(pattern);
    if (!new_node->pattern)
    {
        fprintf(stderr, "Memory allocation failed for exclude pattern: %s\n", pattern);
        free(new_node);
        pthread_mutex_unlock(&excludes->mutex);
        return;
    }

    new_node->next = excludes->buckets[bucket];
    excludes->buckets[bucket] = new_node;
    excludes->count++;

    pthread_mutex_unlock(&excludes->mutex);
}

void free_exclude_list(ExcludeList *excludes)
{
    pthread_mutex_lock(&excludes->mutex);

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

    pthread_mutex_unlock(&excludes->mutex);
    pthread_mutex_destroy(&excludes->mutex);
}

int is_excluded(const char *path, ExcludeList *excludes)
{
    pthread_mutex_lock(&excludes->mutex);

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
                pthread_mutex_unlock(&excludes->mutex);
                return 1;
            }
            current = current->next;
        }
    }

    // Check basename match - handle both forward and backward slashes
    const char *basename = strrchr(path, PATH_SEP);
#ifdef _WIN32
    // On Windows, also check for forward slash
    const char *basename_fwd = strrchr(path, '/');
    if (basename_fwd && (!basename || basename_fwd > basename))
    {
        basename = basename_fwd;
    }
#endif

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
                    pthread_mutex_unlock(&excludes->mutex);
                    return 1;
                }
                current = current->next;
            }
        }
    }

    pthread_mutex_unlock(&excludes->mutex);
    return 0;
}

static int safe_path_join(char *dest, size_t dest_size, const char *path1, const char *path2)
{
    if (dest_size == 0)
        return -1;
    size_t len1 = strlen(path1);
    size_t len2 = strlen(path2);
    size_t required_size = len1 + (len1 > 0 ? 1 : 0) + len2 + 1;

    if (required_size > dest_size)
    {
        return -1;
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
#ifdef _WIN32
    // On Windows, use binary mode and handle file access differently
    FILE *file = fopen(filepath, "rb");
#else
    FILE *file = fopen(filepath, "rb");
#endif

    if (!file)
    {
        return -1;
    }

    unsigned char buffer[BINARY_CHECK_SIZE];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);

    if (bytes_read == 0)
    {
        return 0; // Empty file is considered text
    }

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
        else if (byte < 32 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f' && byte != '\v')
        {
            // Allow more control characters that are common in text files
            control_count++;
        }
        else if (byte > 127)
        {
            high_bit_count++;
        }
    }

    // More lenient binary detection
    // Any null bytes indicate binary
    if (null_count > 0)
        return 1;

    // Too many control characters indicate binary
    if (control_count > bytes_read / 10) // Changed from /20 to /10 for stricter detection
        return 1;

    // Too many high-bit characters might indicate binary (but could be UTF-8)
    if (high_bit_count > bytes_read * 3 / 4) // More lenient for UTF-8
        return 1;

    return 0;
}

// Inode tracker implementation for symlink loop detection
int init_inode_tracker(InodeTracker *tracker)
{
    tracker->head = NULL;
    if (pthread_mutex_init(&tracker->mutex, NULL) != 0)
        return -1;
    return 0;
}

int add_inode(InodeTracker *tracker, dev_t device, ino_t inode)
{
    pthread_mutex_lock(&tracker->mutex);

    // Check if inode already exists
    InodeNode *current = tracker->head;
    while (current)
    {
        if (current->device == device && current->inode == inode)
        {
            pthread_mutex_unlock(&tracker->mutex);
            return 1; // Already exists (loop detected)
        }
        current = current->next;
    }

    // Add new inode
    InodeNode *new_node = malloc(sizeof(InodeNode));
    if (!new_node)
    {
        pthread_mutex_unlock(&tracker->mutex);
        return -1;
    }

    new_node->device = device;
    new_node->inode = inode;
    new_node->next = tracker->head;
    tracker->head = new_node;

    pthread_mutex_unlock(&tracker->mutex);
    return 0; // Added successfully
}

int has_inode(InodeTracker *tracker, dev_t device, ino_t inode)
{
    pthread_mutex_lock(&tracker->mutex);

    InodeNode *current = tracker->head;
    while (current)
    {
        if (current->device == device && current->inode == inode)
        {
            pthread_mutex_unlock(&tracker->mutex);
            return 1; // Found
        }
        current = current->next;
    }

    pthread_mutex_unlock(&tracker->mutex);
    return 0; // Not found
}

void free_inode_tracker(InodeTracker *tracker)
{
    pthread_mutex_lock(&tracker->mutex);

    InodeNode *current = tracker->head;
    while (current)
    {
        InodeNode *next = current->next;
        free(current);
        current = next;
    }
    tracker->head = NULL;

    pthread_mutex_unlock(&tracker->mutex);
    pthread_mutex_destroy(&tracker->mutex);
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

#endif

// Enhanced directory processing with proper symlink handling
static void process_directory_recursive(const char *base_path, const char *current_path,
                                        ExcludeList *excludes, BinaryHandling binary_handling,
                                        SymlinkHandling symlink_handling, FILE *output_file,
                                        InodeTracker *inode_tracker, int show_size, int level,
                                        unsigned long long *total_size, int write_structure
#ifdef WITH_PLUGINS
                                        ,
                                        PluginManager *plugin_manager
#endif
)
{
    char path[MAX_PATH];
    if (safe_path_join(path, sizeof(path), base_path, current_path) < 0)
    {
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    WIN32_FIND_DATAW findData;
    HANDLE hFind;

    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path)
        return;

    wchar_t search_pattern[MAX_PATH];
    swprintf(search_pattern, MAX_PATH, L"%ls\\*", wide_path);
    free(wide_path);

    hFind = FindFirstFileW(search_pattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        char *utf8_filename = wide_to_utf8(findData.cFileName);
        if (!utf8_filename)
            continue;

        char new_relative_path[MAX_PATH];
        if (strlen(current_path) > 0)
        {
            if (safe_path_join(new_relative_path, sizeof(new_relative_path), current_path, utf8_filename) < 0)
            {
                free(utf8_filename);
                continue;
            }
        }
        else
        {
            strncpy(new_relative_path, utf8_filename, sizeof(new_relative_path) - 1);
            new_relative_path[sizeof(new_relative_path) - 1] = '\0';
        }

        if (is_excluded(new_relative_path, excludes))
        {
            free(utf8_filename);
            continue;
        }

        if (write_structure)
        {
            // Generate structure output
            char structure_line[MAX_PATH + 100];
            int indent_len = level * 2;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                snprintf(structure_line, sizeof(structure_line), "%*s📁 %s/\n",
                         indent_len, "", utf8_filename);
                fprintf(output_file, "%s", structure_line);

                // Recurse into subdirectory
                process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                            symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                            ,
                                            plugin_manager
#endif
                );
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
                    snprintf(structure_line, sizeof(structure_line), "%*s📄 [%s] %s\n",
                             indent_len, "", size_buf, utf8_filename);
                }
                else
                {
                    snprintf(structure_line, sizeof(structure_line), "%*s📄 %s\n",
                             indent_len, "", utf8_filename);
                }

                fprintf(output_file, "%s", structure_line);
                *total_size += fileSize.QuadPart;
            }
        }
        else
        {
            // File content processing
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                char new_full_path[MAX_PATH];
                if (safe_path_join(new_full_path, sizeof(new_full_path), base_path, new_relative_path) < 0)
                {
                    free(utf8_filename);
                    continue;
                }

                // Check if file is binary
                int is_binary = is_binary_file(new_full_path);
                if (is_binary == 1)
                {
                    if (binary_handling == BINARY_SKIP)
                    {
                        if (is_verbose())
                            fprintf(stderr, "[fconcat] Skipping binary file: %s\n", new_relative_path);
                        free(utf8_filename);
                        continue;
                    }
                    else if (binary_handling == BINARY_PLACEHOLDER)
                    {
                        fprintf(output_file, "// File: %s\n// [Binary file - content not displayed]\n\n", new_relative_path);
                        free(utf8_filename);
                        continue;
                    }
                }

                // Read and process file content
                FILE *file = fopen(new_full_path, "rb");
                if (file)
                {
                    fprintf(output_file, "// File: %s\n", new_relative_path);

                    char buffer[PLUGIN_CHUNK_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                    {
#ifdef WITH_PLUGINS
                        char *processed_data = NULL;
                        size_t processed_size = 0;

                        // Process through plugins if available
                        if (plugin_manager)
                        {
                            if (process_file_through_plugins(plugin_manager, new_relative_path,
                                                             buffer, bytes_read, &processed_data, &processed_size) == 0)
                            {
                                if (processed_data && processed_size > 0)
                                {
                                    fwrite(processed_data, 1, processed_size, output_file);
                                    free(processed_data);
                                }
                            }
                            else
                            {
                                // Plugin processing failed, write original data
                                fwrite(buffer, 1, bytes_read, output_file);
                            }
                        }
                        else
                        {
                            // No plugins, write original data
                            fwrite(buffer, 1, bytes_read, output_file);
                        }
#else
                        // No plugin support, write original data
                        fwrite(buffer, 1, bytes_read, output_file);
#endif
                    }

                    fprintf(output_file, "\n\n");
                    fclose(file);
                }
                else
                {
                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Cannot open file: %s\n", new_full_path);
                }
            }
            else
            {
                // Recurse into subdirectory
                process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                            symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                            ,
                                            plugin_manager
#endif
                );
            }
        }

        free(utf8_filename);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
#else
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *dp;
    struct stat statbuf;

    while ((dp = readdir(dir)) != NULL)
    {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        char new_relative_path[MAX_PATH];
        char new_full_path[MAX_PATH];

        if (strlen(current_path) > 0)
        {
            if (safe_path_join(new_relative_path, sizeof(new_relative_path), current_path, dp->d_name) < 0)
                continue;
        }
        else
        {
            strncpy(new_relative_path, dp->d_name, sizeof(new_relative_path) - 1);
            new_relative_path[sizeof(new_relative_path) - 1] = '\0';
        }

        if (safe_path_join(new_full_path, sizeof(new_full_path), path, dp->d_name) < 0)
            continue;

        if (is_excluded(new_relative_path, excludes))
        {
            continue;
        }

        // Use lstat to handle symlinks properly
        if (lstat(new_full_path, &statbuf) == -1)
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Cannot access: %s (%s)\n", new_full_path, strerror(errno));
            continue;
        }

        if (write_structure)
        {
            // Generate structure output
            char structure_line[MAX_PATH + 100];
            int indent_len = level * 2;

            if (S_ISLNK(statbuf.st_mode))
            {
                // Handle symbolic link
                struct stat target_stat;
                int stat_result = stat(new_full_path, &target_stat);

                if (stat_result == -1)
                {
                    // Broken symlink
                    snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [BROKEN LINK]\n",
                             indent_len, "", dp->d_name);
                    fprintf(output_file, "%s", structure_line);
                }
                else
                {
                    // Valid symlink
                    if (symlink_handling == SYMLINK_SKIP)
                    {
                        snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [SYMLINK SKIPPED]\n",
                                 indent_len, "", dp->d_name);
                        fprintf(output_file, "%s", structure_line);
                    }
                    else if (symlink_handling == SYMLINK_PLACEHOLDER)
                    {
                        if (S_ISDIR(target_stat.st_mode))
                        {
                            snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s/ -> [SYMLINK TO DIR]\n",
                                     indent_len, "", dp->d_name);
                        }
                        else
                        {
                            if (show_size)
                            {
                                char size_buf[32];
                                format_size(target_stat.st_size, size_buf, sizeof(size_buf));
                                snprintf(structure_line, sizeof(structure_line), "%*s🔗 [%s] %s -> [SYMLINK]\n",
                                         indent_len, "", size_buf, dp->d_name);
                            }
                            else
                            {
                                snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [SYMLINK]\n",
                                         indent_len, "", dp->d_name);
                            }
                            *total_size += target_stat.st_size;
                        }
                        fprintf(output_file, "%s", structure_line);
                    }
                    else if (symlink_handling == SYMLINK_FOLLOW || symlink_handling == SYMLINK_INCLUDE)
                    {
                        // Check for loops
                        if (has_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino))
                        {
                            snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [LOOP DETECTED]\n",
                                     indent_len, "", dp->d_name);
                            fprintf(output_file, "%s", structure_line);
                        }
                        else
                        {
                            // Add inode to tracker
                            add_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino);

                            if (S_ISDIR(target_stat.st_mode) && symlink_handling == SYMLINK_FOLLOW)
                            {
                                snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s/ -> [FOLLOWING]\n",
                                         indent_len, "", dp->d_name);
                                fprintf(output_file, "%s", structure_line);

                                // Recurse into symlinked directory
                                process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                                            symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                                            ,
                                                            plugin_manager
#endif
                                );
                            }
                            else if (!S_ISDIR(target_stat.st_mode))
                            {
                                if (show_size)
                                {
                                    char size_buf[32];
                                    format_size(target_stat.st_size, size_buf, sizeof(size_buf));
                                    snprintf(structure_line, sizeof(structure_line), "%*s🔗 [%s] %s\n",
                                             indent_len, "", size_buf, dp->d_name);
                                }
                                else
                                {
                                    snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s\n",
                                             indent_len, "", dp->d_name);
                                }
                                fprintf(output_file, "%s", structure_line);
                                *total_size += target_stat.st_size;
                            }
                        }
                    }
                }
            }
            else if (S_ISDIR(statbuf.st_mode))
            {
                snprintf(structure_line, sizeof(structure_line), "%*s📁 %s/\n",
                         indent_len, "", dp->d_name);
                fprintf(output_file, "%s", structure_line);

                // Recurse into subdirectory
                process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                            symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                            ,
                                            plugin_manager
#endif
                );
            }
            else
            {
                if (show_size)
                {
                    char size_buf[32];
                    format_size(statbuf.st_size, size_buf, sizeof(size_buf));
                    snprintf(structure_line, sizeof(structure_line), "%*s📄 [%s] %s\n",
                             indent_len, "", size_buf, dp->d_name);
                }
                else
                {
                    snprintf(structure_line, sizeof(structure_line), "%*s📄 %s\n",
                             indent_len, "", dp->d_name);
                }

                fprintf(output_file, "%s", structure_line);
                *total_size += statbuf.st_size;
            }
        }
        else
        {
            // File content processing
            if (S_ISLNK(statbuf.st_mode))
            {
                if (symlink_handling == SYMLINK_SKIP)
                {
                    continue;
                }

                // Check if symlink is valid
                struct stat target_stat;
                if (stat(new_full_path, &target_stat) == -1)
                {
                    // Broken symlink
                    if (symlink_handling == SYMLINK_PLACEHOLDER)
                    {
                        fprintf(output_file, "// File: %s\n// [Broken symlink - target not accessible]\n\n", new_relative_path);
                    }
                    continue;
                }

                // Valid symlink
                if (symlink_handling == SYMLINK_FOLLOW || symlink_handling == SYMLINK_INCLUDE)
                {
                    // Check for loops
                    if (has_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino))
                    {
                        if (is_verbose())
                            fprintf(stderr, "[fconcat] Symlink loop detected: %s\n", new_relative_path);
                        continue;
                    }

                    add_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino);

                    if (S_ISDIR(target_stat.st_mode) && symlink_handling == SYMLINK_FOLLOW)
                    {
                        process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                                    symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                                    ,
                                                    plugin_manager
#endif
                        );
                    }
                    else if (!S_ISDIR(target_stat.st_mode))
                    {
                        // Process symlinked file
                        int is_binary = is_binary_file(new_full_path);
                        if (is_binary == 1)
                        {
                            if (binary_handling == BINARY_SKIP)
                            {
                                continue;
                            }
                            else if (binary_handling == BINARY_PLACEHOLDER)
                            {
                                fprintf(output_file, "// File: %s\n// [Binary symlink file - content not displayed]\n\n", new_relative_path);
                                continue;
                            }
                        }

                        FILE *file = fopen(new_full_path, "rb");
                        if (file)
                        {
                            fprintf(output_file, "// File: %s (symlink)\n", new_relative_path);

                            char buffer[PLUGIN_CHUNK_SIZE];
                            size_t bytes_read;
                            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                            {
#ifdef WITH_PLUGINS
                                char *processed_data = NULL;
                                size_t processed_size = 0;

                                // Process through plugins if available
                                if (plugin_manager)
                                {
                                    if (process_file_through_plugins(plugin_manager, new_relative_path,
                                                                     buffer, bytes_read, &processed_data, &processed_size) == 0)
                                    {
                                        if (processed_data && processed_size > 0)
                                        {
                                            fwrite(processed_data, 1, processed_size, output_file);
                                            free(processed_data);
                                        }
                                    }
                                    else
                                    {
                                        // Plugin processing failed, write original data
                                        fwrite(buffer, 1, bytes_read, output_file);
                                    }
                                }
                                else
                                {
                                    // No plugins, write original data
                                    fwrite(buffer, 1, bytes_read, output_file);
                                }
#else
                                // No plugin support, write original data
                                fwrite(buffer, 1, bytes_read, output_file);
#endif
                            }

                            fprintf(output_file, "\n\n");
                            fclose(file);
                        }
                    }
                }
                else if (symlink_handling == SYMLINK_PLACEHOLDER)
                {
                    fprintf(output_file, "// File: %s\n// [Symlink - content not followed]\n\n", new_relative_path);
                }
            }
            else if (S_ISDIR(statbuf.st_mode))
            {
                process_directory_recursive(base_path, new_relative_path, excludes, binary_handling,
                                            symlink_handling, output_file, inode_tracker, show_size, level + 1, total_size, write_structure
#ifdef WITH_PLUGINS
                                            ,
                                            plugin_manager
#endif
                );
            }
            else
            {
                // Process regular file
                int is_binary = is_binary_file(new_full_path);
                if (is_binary == 1)
                {
                    if (binary_handling == BINARY_SKIP)
                    {
                        continue;
                    }
                    else if (binary_handling == BINARY_PLACEHOLDER)
                    {
                        fprintf(output_file, "// File: %s\n// [Binary file - content not displayed]\n\n", new_relative_path);
                        continue;
                    }
                }

                FILE *file = fopen(new_full_path, "rb");
                if (file)
                {
                    fprintf(output_file, "// File: %s\n", new_relative_path);

                    char buffer[PLUGIN_CHUNK_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                    {
#ifdef WITH_PLUGINS
                        char *processed_data = NULL;
                        size_t processed_size = 0;

                        // Process through plugins if available
                        if (plugin_manager)
                        {
                            if (process_file_through_plugins(plugin_manager, new_relative_path,
                                                             buffer, bytes_read, &processed_data, &processed_size) == 0)
                            {
                                if (processed_data && processed_size > 0)
                                {
                                    fwrite(processed_data, 1, processed_size, output_file);
                                    free(processed_data);
                                }
                            }
                            else
                            {
                                // Plugin processing failed, write original data
                                fwrite(buffer, 1, bytes_read, output_file);
                            }
                        }
                        else
                        {
                            // No plugins, write original data
                            fwrite(buffer, 1, bytes_read, output_file);
                        }
#else
                        // No plugin support, write original data
                        fwrite(buffer, 1, bytes_read, output_file);
#endif
                    }

                    fprintf(output_file, "\n\n");
                    fclose(file);
                }
            }
        }
    }

    closedir(dir);
#endif
}

int process_directory(ProcessingContext *ctx)
{
    if (is_verbose())
        fprintf(stderr, "[fconcat] Starting directory processing\n");

    // Initialize inode tracker for symlink loop detection
    InodeTracker inode_tracker;
    if (init_inode_tracker(&inode_tracker) != 0)
    {
        fprintf(stderr, "Error initializing inode tracker\n");
        return -1;
    }

    // Write directory structure header
    fprintf(ctx->output_file, "Directory Structure:\n==================\n\n");

    // Process directory structure
    unsigned long long total_size = 0;
    process_directory_recursive(ctx->base_path, "", ctx->excludes, ctx->binary_handling,
                                ctx->symlink_handling, ctx->output_file, &inode_tracker, ctx->show_size, 0, &total_size, 1
#ifdef WITH_PLUGINS
                                ,
                                ctx->plugin_manager
#endif
    );

    // Write total size if requested
    if (ctx->show_size)
    {
        char size_buf[32];
        format_size(total_size, size_buf, sizeof(size_buf));
        fprintf(ctx->output_file, "\nTotal Size: %s (%llu bytes)\n", size_buf, total_size);
    }

    // Write file contents header
    fprintf(ctx->output_file, "\nFile Contents:\n=============\n\n");

    // Reset inode tracker for file concatenation
    free_inode_tracker(&inode_tracker);
    if (init_inode_tracker(&inode_tracker) != 0)
    {
        fprintf(stderr, "Error reinitializing inode tracker\n");
        return -1;
    }

    // Process file contents
    process_directory_recursive(ctx->base_path, "", ctx->excludes, ctx->binary_handling,
                                ctx->symlink_handling, ctx->output_file, &inode_tracker, ctx->show_size, 0, &total_size, 0
#ifdef WITH_PLUGINS
                                ,
                                ctx->plugin_manager
#endif
    );

    // Cleanup inode tracker
    free_inode_tracker(&inode_tracker);

    if (is_verbose())
        fprintf(stderr, "[fconcat] Directory processing complete\n");

    return 0;
}