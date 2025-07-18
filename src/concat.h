// File: src/concat.h
#ifndef CONCAT_H
#define CONCAT_H

#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef WITH_PLUGINS
#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h>
#endif
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wchar.h>
#else
#define MAX_PATH 4096
#endif

#define BUFFER_SIZE 4096
#define MAX_EXCLUDES 1000
#define BINARY_CHECK_SIZE 8192

#ifdef WITH_PLUGINS
#define MAX_PLUGINS 32
#define PLUGIN_CHUNK_SIZE 4096
#else
#define PLUGIN_CHUNK_SIZE 4096 // Define it even without plugins for buffer size
#endif

#if defined(_WIN32) || defined(_WIN64)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

#ifdef WITH_PLUGINS
// Plugin system structures
typedef struct PluginContext
{
    void *private_data;
    const char *file_path;
    size_t total_processed;
    int plugin_index;
} PluginContext;

typedef struct StreamingPlugin
{
    const char *name;
    const char *version;

    // Lifecycle
    int (*init)(void);
    void (*cleanup)(void);

    // Per-file processing
    PluginContext *(*file_start)(const char *relative_path);
    int (*process_chunk)(PluginContext *ctx, const char *input, size_t input_size,
                         char **output, size_t *output_size);
    int (*file_end)(PluginContext *ctx, char **final_output, size_t *final_size);
    void (*file_cleanup)(PluginContext *ctx);

    // Plugin metadata
    void *handle; // dlopen handle
    int index;    // Plugin index in array
} StreamingPlugin;

typedef struct PluginManager
{
    StreamingPlugin *plugins[MAX_PLUGINS];
    int count;
    int initialized;
    pthread_mutex_t mutex;
} PluginManager;
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
    pthread_mutex_t mutex;
} ExcludeList;

typedef enum
{
    BINARY_SKIP,
    BINARY_INCLUDE,
    BINARY_PLACEHOLDER
} BinaryHandling;

typedef enum
{
    SYMLINK_SKIP,
    SYMLINK_FOLLOW,
    SYMLINK_INCLUDE,
    SYMLINK_PLACEHOLDER
} SymlinkHandling;

// Inode tracking for symlink loop detection
typedef struct InodeNode
{
    dev_t device;
    ino_t inode;
    struct InodeNode *next;
} InodeNode;

typedef struct
{
    InodeNode *head;
    pthread_mutex_t mutex;
} InodeTracker;

// Processing context
typedef struct
{
    const char *base_path;
    ExcludeList *excludes;
    BinaryHandling binary_handling;
    SymlinkHandling symlink_handling;
    int show_size;
    FILE *output_file;
#ifdef WITH_PLUGINS
    PluginManager *plugin_manager;
#endif
    int interactive_mode;
} ProcessingContext;

#ifdef WITH_PLUGINS
// Plugin system functions
int init_plugin_manager(PluginManager *manager);
void destroy_plugin_manager(PluginManager *manager);
int load_plugin(PluginManager *manager, const char *plugin_path);
int process_file_through_plugins(PluginManager *manager, const char *relative_path,
                                 const char *input_data, size_t input_size,
                                 char **output_data, size_t *output_size);
#endif

// Core functions
void init_exclude_list(ExcludeList *excludes);
void add_exclude_pattern(ExcludeList *excludes, const char *pattern);
void free_exclude_list(ExcludeList *excludes);
int is_excluded(const char *path, ExcludeList *excludes);
void format_size(unsigned long long size, char *buffer, size_t buffer_size);
int is_binary_file(const char *filepath);
int init_inode_tracker(InodeTracker *tracker);
int add_inode(InodeTracker *tracker, dev_t device, ino_t inode);
int has_inode(InodeTracker *tracker, dev_t device, ino_t inode);
void free_inode_tracker(InodeTracker *tracker);
int process_directory(ProcessingContext *ctx);

#if defined(_WIN32) || defined(_WIN64)
wchar_t *utf8_to_wide(const char *utf8_path);
char *wide_to_utf8(const wchar_t *wide_path);
#else
#define utf8_to_wide(x) (x)
#define wide_to_utf8(x) (x)
#endif

#endif