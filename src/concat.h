#ifndef CONCAT_H
#define CONCAT_H

#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef PATH_MAX
#define PATH_MAX 4096 // Default value if PATH_MAX is not defined
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <wchar.h>
// On Windows, MAX_PATH is already defined by windows.h as 260
#else
#define MAX_PATH 4096
#endif

#define BUFFER_SIZE 4096
#define MAX_EXCLUDES 1000      
#define BINARY_CHECK_SIZE 8192 // Bytes to check for binary detection

// Threading configuration
#define MAX_THREADS 24
#define DEFAULT_WORKER_THREADS 4
#define WORK_QUEUE_SIZE 10000
#define OUTPUT_BUFFER_SIZE 1048576 // 1MB output buffer
#define STREAM_CHUNK_SIZE 65536    // 64KB streaming chunks

#if defined(_WIN32) || defined(_WIN64)
#define PATH_SEP '\\'
// Windows headers already included above
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
    SYMLINK_SKIP,       // Skip all symlinks (safe default)
    SYMLINK_FOLLOW,     // Follow symlinks (with loop detection)
    SYMLINK_INCLUDE,    // Include symlink targets as files
    SYMLINK_PLACEHOLDER // Show symlinks but don't follow
} SymlinkHandling;

// Work item types
typedef enum
{
    WORK_DIRECTORY,
    WORK_FILE,
    WORK_POISON // Signals thread to exit
} WorkType;

// Work item for processing queue
typedef struct WorkItem
{
    WorkType type;
    char *full_path;
    char *relative_path;
    int level; // For directory structure indentation
    size_t file_size;
    struct WorkItem *next;
} WorkItem;

// Thread-safe work queue
typedef struct
{
    WorkItem *head;
    WorkItem *tail;
    size_t count;
    size_t max_size;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    atomic_bool shutdown;
} WorkQueue;

// Output chunk for streaming
typedef struct OutputChunk
{
    char *data;
    size_t size;
    size_t capacity;
    uint64_t sequence_number; // For ordering
    struct OutputChunk *next;
} OutputChunk;

// Thread-safe output buffer for streaming
typedef struct
{
    OutputChunk *head;
    OutputChunk *tail;
    size_t total_chunks;
    uint64_t next_sequence;
    uint64_t expected_sequence;
    pthread_mutex_t mutex;
    pthread_cond_t data_available;
    atomic_bool finished;
    FILE *output_file;
    pthread_t writer_thread;
} StreamingOutput;

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

// Thread pool for workers
typedef struct
{
    pthread_t *threads;
    int num_threads;
    WorkQueue *work_queue;
    ExcludeList *excludes;
    BinaryHandling binary_handling;
    SymlinkHandling symlink_handling;
    StreamingOutput *output;
    InodeTracker *inode_tracker;
    atomic_int active_workers;
    atomic_bool shutdown;

    // Statistics
    atomic_ulong files_processed;
    atomic_ulong directories_processed;
    atomic_ulong bytes_processed;
    atomic_ulong files_skipped;
    atomic_ulong symlinks_processed;
    atomic_ulong symlinks_skipped;

    // For directory structure output
    pthread_mutex_t structure_mutex;
    int show_size;
    atomic_ullong total_size;
} ThreadPool;

// Processing context
typedef struct
{
    const char *base_path;
    ExcludeList *excludes;
    BinaryHandling binary_handling;
    SymlinkHandling symlink_handling;
    int show_size;
    int num_threads;
    StreamingOutput *output;
    ThreadPool *pool;
} ProcessingContext;

/**
 * Initialize an ExcludeList with thread safety.
 */
void init_exclude_list(ExcludeList *excludes);

/**
 * Add a pattern to the ExcludeList (thread-safe).
 */
void add_exclude_pattern(ExcludeList *excludes, const char *pattern);

/**
 * Free memory used by ExcludeList.
 */
void free_exclude_list(ExcludeList *excludes);

/**
 * Check if a path is excluded (thread-safe).
 */
int is_excluded(const char *path, ExcludeList *excludes);

/**
 * Format file size to human-readable format (KB, MB, GB, etc.)
 */
void format_size(unsigned long long size, char *buffer, size_t buffer_size);

/**
 * Check if a file appears to be binary.
 */
int is_binary_file(const char *filepath);

/**
 * Initialize inode tracker for symlink loop detection.
 */
int init_inode_tracker(InodeTracker *tracker);

/**
 * Add inode to tracker.
 */
int add_inode(InodeTracker *tracker, dev_t device, ino_t inode);

/**
 * Check if inode already exists in tracker.
 */
int has_inode(InodeTracker *tracker, dev_t device, ino_t inode);

/**
 * Free inode tracker.
 */
void free_inode_tracker(InodeTracker *tracker);

/**
 * Initialize work queue.
 */
int init_work_queue(WorkQueue *queue, size_t max_size);

/**
 * Destroy work queue.
 */
void destroy_work_queue(WorkQueue *queue);

/**
 * Add work item to queue (blocking if full).
 */
int enqueue_work(WorkQueue *queue, WorkItem *item);

/**
 * Get work item from queue (blocking if empty).
 */
WorkItem *dequeue_work(WorkQueue *queue);

/**
 * Create work item.
 */
WorkItem *create_work_item(WorkType type, const char *full_path, const char *relative_path, int level, size_t file_size);

/**
 * Free work item.
 */
void free_work_item(WorkItem *item);

/**
 * Initialize streaming output.
 */
int init_streaming_output(StreamingOutput *output, FILE *output_file);

/**
 * Destroy streaming output.
 */
void destroy_streaming_output(StreamingOutput *output);

/**
 * Add data to streaming output.
 */
int stream_output_write(StreamingOutput *output, const char *data, size_t size);

/**
 * Signal that no more data will be written.
 */
void stream_output_finish(StreamingOutput *output);

/**
 * Initialize thread pool.
 */
int init_thread_pool(ThreadPool *pool, int num_threads, WorkQueue *work_queue,
                     ExcludeList *excludes, BinaryHandling binary_handling,
                     SymlinkHandling symlink_handling, StreamingOutput *output,
                     InodeTracker *inode_tracker, int show_size);

/**
 * Destroy thread pool.
 */
void destroy_thread_pool(ThreadPool *pool);

/**
 * Process directory structure and files with multi-threading.
 */
int process_directory_threaded(ProcessingContext *ctx);

/**
 * Print processing statistics.
 */
void print_processing_stats(ThreadPool *pool);

/**
 * Convert UTF-8 string to platform-appropriate format for file operations.
 */
#if defined(_WIN32) || defined(_WIN64)
wchar_t *utf8_to_wide(const char *utf8_path);
char *wide_to_utf8(const wchar_t *wide_path);
#else
#define utf8_to_wide(x) (x)
#define wide_to_utf8(x) (x)
#endif

#endif