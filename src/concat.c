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
    for (const char *p = pattern; *p && i < sizeof(norm_pattern) - 1; p++, i++) {
        norm_pattern[i] = (*p == '\\') ? '/' : tolower(*p);
    }
    norm_pattern[i] = '\0';
    
    // Normalize string
    i = 0;
    for (const char *s = string; *s && i < sizeof(norm_string) - 1; s++, i++) {
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
    if (basename_fwd && (!basename || basename_fwd > basename)) {
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
    if (control_count > bytes_read / 10)  // Changed from /20 to /10 for stricter detection
        return 1;
    
    // Too many high-bit characters might indicate binary (but could be UTF-8)
    if (high_bit_count > bytes_read * 3 / 4)  // More lenient for UTF-8
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

// Work queue implementation
int init_work_queue(WorkQueue *queue, size_t max_size)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->max_size = max_size;
    atomic_store(&queue->shutdown, false);

    if (pthread_mutex_init(&queue->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&queue->not_empty, NULL) != 0)
    {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    if (pthread_cond_init(&queue->not_full, NULL) != 0)
    {
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->not_empty);
        return -1;
    }

    return 0;
}

void destroy_work_queue(WorkQueue *queue)
{
    atomic_store(&queue->shutdown, true);

    pthread_mutex_lock(&queue->mutex);
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    // Clean up remaining work items
    while (queue->head)
    {
        WorkItem *item = queue->head;
        queue->head = item->next;
        free_work_item(item);
    }

    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
}

int enqueue_work(WorkQueue *queue, WorkItem *item)
{
    pthread_mutex_lock(&queue->mutex);

    while (queue->count >= queue->max_size && !atomic_load(&queue->shutdown))
    {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    if (atomic_load(&queue->shutdown))
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    item->next = NULL;
    if (queue->tail)
    {
        queue->tail->next = item;
    }
    else
    {
        queue->head = item;
    }
    queue->tail = item;
    queue->count++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

WorkItem *dequeue_work(WorkQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);

    while (queue->head == NULL && !atomic_load(&queue->shutdown))
    {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->head == NULL)
    {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    WorkItem *item = queue->head;
    queue->head = item->next;
    if (queue->head == NULL)
    {
        queue->tail = NULL;
    }
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return item;
}

WorkItem *create_work_item(WorkType type, const char *full_path, const char *relative_path, int level, size_t file_size)
{
    WorkItem *item = malloc(sizeof(WorkItem));
    if (!item)
        return NULL;

    item->type = type;
    item->full_path = full_path ? strdup(full_path) : NULL;
    item->relative_path = relative_path ? strdup(relative_path) : NULL;
    item->level = level;
    item->file_size = file_size;
    item->next = NULL;

    return item;
}

void free_work_item(WorkItem *item)
{
    if (item)
    {
        free(item->full_path);
        free(item->relative_path);
        free(item);
    }
}

// Streaming output implementation
static void *output_writer_thread(void *arg)
{
    StreamingOutput *output = (StreamingOutput *)arg;

    while (!atomic_load(&output->finished) || output->head != NULL)
    {
        pthread_mutex_lock(&output->mutex);

        while (output->head == NULL && !atomic_load(&output->finished))
        {
            pthread_cond_wait(&output->data_available, &output->mutex);
        }

        // Process chunks in sequence order
        while (output->head && output->head->sequence_number == output->expected_sequence)
        {
            OutputChunk *chunk = output->head;
            output->head = chunk->next;
            if (output->head == NULL)
            {
                output->tail = NULL;
            }
            output->total_chunks--;
            output->expected_sequence++;

            pthread_mutex_unlock(&output->mutex);

            // Write chunk to file
            if (fwrite(chunk->data, 1, chunk->size, output->output_file) != chunk->size)
            {
                fprintf(stderr, "Error writing to output file\n");
            }
            fflush(output->output_file); // Ensure data is written

            free(chunk->data);
            free(chunk);

            pthread_mutex_lock(&output->mutex);
        }

        pthread_mutex_unlock(&output->mutex);

        // Small delay to prevent busy waiting
        usleep(1000);
    }

    return NULL;
}

int init_streaming_output(StreamingOutput *output, FILE *output_file)
{
    output->head = NULL;
    output->tail = NULL;
    output->total_chunks = 0;
    output->next_sequence = 0;
    output->expected_sequence = 0;
    output->output_file = output_file;
    atomic_store(&output->finished, false);

    if (pthread_mutex_init(&output->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&output->data_available, NULL) != 0)
    {
        pthread_mutex_destroy(&output->mutex);
        return -1;
    }

    if (pthread_create(&output->writer_thread, NULL, output_writer_thread, output) != 0)
    {
        pthread_cond_destroy(&output->data_available);
        pthread_mutex_destroy(&output->mutex);
        return -1;
    }

    return 0;
}

void destroy_streaming_output(StreamingOutput *output)
{
    atomic_store(&output->finished, true);

    pthread_mutex_lock(&output->mutex);
    pthread_cond_broadcast(&output->data_available);
    pthread_mutex_unlock(&output->mutex);

    pthread_join(output->writer_thread, NULL);

    // Clean up remaining chunks
    while (output->head)
    {
        OutputChunk *chunk = output->head;
        output->head = chunk->next;
        free(chunk->data);
        free(chunk);
    }

    pthread_cond_destroy(&output->data_available);
    pthread_mutex_destroy(&output->mutex);
}

int stream_output_write(StreamingOutput *output, const char *data, size_t size)
{
    if (size == 0)
        return 0;

    OutputChunk *chunk = malloc(sizeof(OutputChunk));
    if (!chunk)
        return -1;

    chunk->data = malloc(size);
    if (!chunk->data)
    {
        free(chunk);
        return -1;
    }

    memcpy(chunk->data, data, size);
    chunk->size = size;
    chunk->capacity = size;
    chunk->next = NULL;

    pthread_mutex_lock(&output->mutex);
    chunk->sequence_number = output->next_sequence++;

    if (output->tail)
    {
        output->tail->next = chunk;
    }
    else
    {
        output->head = chunk;
    }
    output->tail = chunk;
    output->total_chunks++;

    pthread_cond_signal(&output->data_available);
    pthread_mutex_unlock(&output->mutex);

    return 0;
}

void stream_output_finish(StreamingOutput *output)
{
    atomic_store(&output->finished, true);

    pthread_mutex_lock(&output->mutex);
    pthread_cond_broadcast(&output->data_available);
    pthread_mutex_unlock(&output->mutex);
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

// Worker thread function
static void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    atomic_fetch_add(&pool->active_workers, 1);

    if (is_verbose())
        fprintf(stderr, "[fconcat] Worker thread started\n");

    while (!atomic_load(&pool->shutdown))
    {
        WorkItem *item = dequeue_work(pool->work_queue);
        if (!item)
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Worker thread got NULL item\n");
            break;
        }

        if (item->type == WORK_POISON)
        {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Worker thread received poison pill\n");
            free_work_item(item);
            break;
        }

        if (is_verbose())
            fprintf(stderr, "[fconcat] Processing work item: %s\n", item->relative_path);

        if (item->type == WORK_DIRECTORY)
        {
            // Process directory - scan and add files/subdirs to queue
            atomic_fetch_add(&pool->directories_processed, 1);

            if (is_verbose())
                fprintf(stderr, "[fconcat] Processing directory: %s\n", item->full_path);

#if defined(_WIN32) || defined(_WIN64)
            WIN32_FIND_DATAW findData;
            HANDLE hFind;

            wchar_t *wide_path = utf8_to_wide(item->full_path);
            if (wide_path)
            {
                wchar_t search_pattern[MAX_PATH];
                swprintf(search_pattern, MAX_PATH, L"%ls\\*", wide_path);
                free(wide_path);

                hFind = FindFirstFileW(search_pattern, &findData);
                if (hFind != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
                            continue;

                        char *utf8_filename = wide_to_utf8(findData.cFileName);
                        if (!utf8_filename)
                            continue;

                        char new_full_path[MAX_PATH];
                        char new_relative_path[MAX_PATH];

                        if (safe_path_join(new_full_path, sizeof(new_full_path), item->full_path, utf8_filename) < 0 ||
                            safe_path_join(new_relative_path, sizeof(new_relative_path), item->relative_path, utf8_filename) < 0)
                        {
                            free(utf8_filename);
                            continue;
                        }

                        if (is_excluded(new_relative_path, pool->excludes))
                        {
                            free(utf8_filename);
                            continue;
                        }

                        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        {
                            WorkItem *dir_item = create_work_item(WORK_DIRECTORY, new_full_path, new_relative_path, item->level + 1, 0);
                            if (dir_item)
                            {
                                if (enqueue_work(pool->work_queue, dir_item) != 0)
                                {
                                    free_work_item(dir_item);
                                }
                            }
                        }
                        else
                        {
                            LARGE_INTEGER fileSize;
                            fileSize.LowPart = findData.nFileSizeLow;
                            fileSize.HighPart = findData.nFileSizeHigh;

                            WorkItem *file_item = create_work_item(WORK_FILE, new_full_path, new_relative_path, item->level + 1, fileSize.QuadPart);
                            if (file_item)
                            {
                                if (enqueue_work(pool->work_queue, file_item) != 0)
                                {
                                    free_work_item(file_item);
                                }
                            }
                        }

                        free(utf8_filename);
                    } while (FindNextFileW(hFind, &findData));

                    FindClose(hFind);
                }
            }
#else
            DIR *dir = opendir(item->full_path);
            if (dir)
            {
                struct dirent *dp;
                struct stat statbuf;

                while ((dp = readdir(dir)) != NULL)
                {
                    if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                        continue;

                    char new_full_path[MAX_PATH];
                    char new_relative_path[MAX_PATH];

                    if (safe_path_join(new_full_path, sizeof(new_full_path), item->full_path, dp->d_name) < 0 ||
                        safe_path_join(new_relative_path, sizeof(new_relative_path), item->relative_path, dp->d_name) < 0)
                    {
                        continue;
                    }

                    if (is_excluded(new_relative_path, pool->excludes))
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

                    // Handle symbolic links
                    if (S_ISLNK(statbuf.st_mode))
                    {
                        atomic_fetch_add(&pool->symlinks_processed, 1);

                        if (pool->symlink_handling == SYMLINK_SKIP)
                        {
                            if (is_verbose())
                                fprintf(stderr, "[fconcat] Skipping symlink: %s\n", new_relative_path);
                            atomic_fetch_add(&pool->symlinks_skipped, 1);
                            continue;
                        }

                        // Check if symlink is valid by following it
                        struct stat target_stat;
                        int stat_result = stat(new_full_path, &target_stat);

                        if (stat_result == -1)
                        {
                            // Broken symlink
                            if (is_verbose())
                                fprintf(stderr, "[fconcat] Broken symlink: %s -> (target not accessible)\n", new_relative_path);

                            if (pool->symlink_handling == SYMLINK_PLACEHOLDER)
                            {
                                // Add broken symlink placeholder to output (this should be handled in structure generation)
                            }
                            atomic_fetch_add(&pool->symlinks_skipped, 1);
                            continue;
                        }

                        // Valid symlink
                        if (pool->symlink_handling == SYMLINK_FOLLOW)
                        {
                            // Check for loops using inode tracking
                            if (has_inode(pool->inode_tracker, target_stat.st_dev, target_stat.st_ino))
                            {
                                if (is_verbose())
                                    fprintf(stderr, "[fconcat] Symlink loop detected: %s\n", new_relative_path);
                                atomic_fetch_add(&pool->symlinks_skipped, 1);
                                continue;
                            }

                            // Add inode to tracker and process target
                            add_inode(pool->inode_tracker, target_stat.st_dev, target_stat.st_ino);

                            if (S_ISDIR(target_stat.st_mode))
                            {
                                WorkItem *dir_item = create_work_item(WORK_DIRECTORY, new_full_path, new_relative_path, item->level + 1, 0);
                                if (dir_item)
                                {
                                    if (enqueue_work(pool->work_queue, dir_item) != 0)
                                    {
                                        free_work_item(dir_item);
                                    }
                                }
                            }
                            else
                            {
                                WorkItem *file_item = create_work_item(WORK_FILE, new_full_path, new_relative_path, item->level + 1, target_stat.st_size);
                                if (file_item)
                                {
                                    if (enqueue_work(pool->work_queue, file_item) != 0)
                                    {
                                        free_work_item(file_item);
                                    }
                                }
                            }
                        }
                        else if (pool->symlink_handling == SYMLINK_INCLUDE && !S_ISDIR(target_stat.st_mode))
                        {
                            // Include symlink target as file
                            WorkItem *file_item = create_work_item(WORK_FILE, new_full_path, new_relative_path, item->level + 1, target_stat.st_size);
                            if (file_item)
                            {
                                if (enqueue_work(pool->work_queue, file_item) != 0)
                                {
                                    free_work_item(file_item);
                                }
                            }
                        }
                        else if (pool->symlink_handling == SYMLINK_PLACEHOLDER)
                        {
                            // Placeholder handling is done in structure generation
                            atomic_fetch_add(&pool->symlinks_skipped, 1);
                        }
                    }
                    else if (S_ISDIR(statbuf.st_mode))
                    {
                        WorkItem *dir_item = create_work_item(WORK_DIRECTORY, new_full_path, new_relative_path, item->level + 1, 0);
                        if (dir_item)
                        {
                            if (enqueue_work(pool->work_queue, dir_item) != 0)
                            {
                                free_work_item(dir_item);
                            }
                        }
                    }
                    else
                    {
                        WorkItem *file_item = create_work_item(WORK_FILE, new_full_path, new_relative_path, item->level + 1, statbuf.st_size);
                        if (file_item)
                        {
                            if (enqueue_work(pool->work_queue, file_item) != 0)
                            {
                                free_work_item(file_item);
                            }
                        }
                    }
                }

                closedir(dir);
            }
            else
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Cannot open directory: %s\n", item->full_path);
            }
#endif
        }
        else if (item->type == WORK_FILE)
        {
            // Process file
            atomic_fetch_add(&pool->files_processed, 1);
            atomic_fetch_add(&pool->bytes_processed, item->file_size);
            atomic_fetch_add(&pool->total_size, item->file_size);

            if (is_verbose())
                fprintf(stderr, "[fconcat] Processing file: %s\n", item->full_path);

            // Check if file is binary
            int is_binary = is_binary_file(item->full_path);
            if (is_binary == 1)
            {
                if (pool->binary_handling == BINARY_SKIP)
                {
                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Skipping binary file: %s\n", item->relative_path);
                    atomic_fetch_add(&pool->files_skipped, 1);
                    free_work_item(item);
                    continue;
                }
                else if (pool->binary_handling == BINARY_PLACEHOLDER)
                {
                    char placeholder[MAX_PATH + 100];
                    snprintf(placeholder, sizeof(placeholder),
                             "// File: %s\n// [Binary file - content not displayed]\n\n",
                             item->relative_path);
                    stream_output_write(pool->output, placeholder, strlen(placeholder));
                    free_work_item(item);
                    continue;
                }
            }

            // Read and stream file content
            FILE *file = fopen(item->full_path, "rb");
            if (file)
            {
                char header[MAX_PATH + 50];
                snprintf(header, sizeof(header), "// File: %s\n", item->relative_path);
                stream_output_write(pool->output, header, strlen(header));

                char buffer[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                {
                    stream_output_write(pool->output, buffer, bytes_read);
                }

                stream_output_write(pool->output, "\n\n", 2);
                fclose(file);
            }
            else
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Cannot open file: %s\n", item->full_path);
                atomic_fetch_add(&pool->files_skipped, 1);
            }
        }

        free_work_item(item);
    }

    atomic_fetch_sub(&pool->active_workers, 1);
    if (is_verbose())
        fprintf(stderr, "[fconcat] Worker thread exiting\n");
    return NULL;
}

int init_thread_pool(ThreadPool *pool, int num_threads, WorkQueue *work_queue,
                     ExcludeList *excludes, BinaryHandling binary_handling,
                     SymlinkHandling symlink_handling, StreamingOutput *output,
                     InodeTracker *inode_tracker, int show_size)
{
    pool->num_threads = num_threads;
    pool->work_queue = work_queue;
    pool->excludes = excludes;
    pool->binary_handling = binary_handling;
    pool->symlink_handling = symlink_handling;
    pool->output = output;
    pool->inode_tracker = inode_tracker;
    pool->show_size = show_size;
    atomic_store(&pool->shutdown, false);
    atomic_store(&pool->active_workers, 0);
    atomic_store(&pool->files_processed, 0);
    atomic_store(&pool->directories_processed, 0);
    atomic_store(&pool->bytes_processed, 0);
    atomic_store(&pool->files_skipped, 0);
    atomic_store(&pool->symlinks_processed, 0);
    atomic_store(&pool->symlinks_skipped, 0);
    atomic_store(&pool->total_size, 0);

    if (pthread_mutex_init(&pool->structure_mutex, NULL) != 0)
        return -1;

    pool->threads = malloc(num_threads * sizeof(pthread_t));
    if (!pool->threads)
    {
        pthread_mutex_destroy(&pool->structure_mutex);
        return -1;
    }

    for (int i = 0; i < num_threads; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0)
        {
            atomic_store(&pool->shutdown, true);
            for (int j = 0; j < i; j++)
            {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->structure_mutex);
            return -1;
        }
    }

    return 0;
}

void destroy_thread_pool(ThreadPool *pool)
{
    if (is_verbose())
        fprintf(stderr, "[fconcat] Shutting down thread pool\n");

    atomic_store(&pool->shutdown, true);

    // Send poison pills to all threads
    for (int i = 0; i < pool->num_threads; i++)
    {
        WorkItem *poison = create_work_item(WORK_POISON, NULL, NULL, 0, 0);
        if (poison)
        {
            // Force enqueue even if queue is full
            pthread_mutex_lock(&pool->work_queue->mutex);
            poison->next = NULL;
            if (pool->work_queue->tail)
            {
                pool->work_queue->tail->next = poison;
            }
            else
            {
                pool->work_queue->head = poison;
            }
            pool->work_queue->tail = poison;
            pool->work_queue->count++;
            pthread_cond_signal(&pool->work_queue->not_empty);
            pthread_mutex_unlock(&pool->work_queue->mutex);
        }
    }

    // Wait for all threads to finish
    for (int i = 0; i < pool->num_threads; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    pthread_mutex_destroy(&pool->structure_mutex);

    if (is_verbose())
        fprintf(stderr, "[fconcat] Thread pool shutdown complete\n");
}

// Enhanced directory processing with proper symlink handling
static void process_directory_simple(const char *base_path, const char *current_path,
                                     ExcludeList *excludes, BinaryHandling binary_handling,
                                     SymlinkHandling symlink_handling, StreamingOutput *output,
                                     InodeTracker *inode_tracker, int show_size, int level,
                                     unsigned long long *total_size)
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

        // Generate structure output
        char structure_line[MAX_PATH + 100];
        int indent_len = level * 2;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            snprintf(structure_line, sizeof(structure_line), "%*s📁 %s/\n",
                     indent_len, "", utf8_filename);
            stream_output_write(output, structure_line, strlen(structure_line));

            // Recurse into subdirectory
            process_directory_simple(base_path, new_relative_path, excludes, binary_handling,
                                     symlink_handling, output, inode_tracker, show_size, level + 1, total_size);
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

            stream_output_write(output, structure_line, strlen(structure_line));
            *total_size += fileSize.QuadPart;
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
                stream_output_write(output, structure_line, strlen(structure_line));

                if (is_verbose())
                    fprintf(stderr, "[fconcat] Broken symlink: %s\n", new_relative_path);
            }
            else
            {
                // Valid symlink
                if (symlink_handling == SYMLINK_SKIP)
                {
                    snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [SYMLINK SKIPPED]\n",
                             indent_len, "", dp->d_name);
                    stream_output_write(output, structure_line, strlen(structure_line));

                    if (is_verbose())
                        fprintf(stderr, "[fconcat] Skipping symlink: %s\n", new_relative_path);
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
                    stream_output_write(output, structure_line, strlen(structure_line));
                }
                else if (symlink_handling == SYMLINK_FOLLOW || symlink_handling == SYMLINK_INCLUDE)
                {
                    // Check for loops
                    if (has_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino))
                    {
                        snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s -> [LOOP DETECTED]\n",
                                 indent_len, "", dp->d_name);
                        stream_output_write(output, structure_line, strlen(structure_line));

                        if (is_verbose())
                            fprintf(stderr, "[fconcat] Symlink loop detected: %s\n", new_relative_path);
                    }
                    else
                    {
                        // Add inode to tracker
                        add_inode(inode_tracker, target_stat.st_dev, target_stat.st_ino);

                        if (S_ISDIR(target_stat.st_mode) && symlink_handling == SYMLINK_FOLLOW)
                        {
                            snprintf(structure_line, sizeof(structure_line), "%*s🔗 %s/ -> [FOLLOWING]\n",
                                     indent_len, "", dp->d_name);
                            stream_output_write(output, structure_line, strlen(structure_line));

                            // Recurse into symlinked directory
                            process_directory_simple(base_path, new_relative_path, excludes, binary_handling,
                                                     symlink_handling, output, inode_tracker, show_size, level + 1, total_size);
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
                            stream_output_write(output, structure_line, strlen(structure_line));
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
            stream_output_write(output, structure_line, strlen(structure_line));

            // Recurse into subdirectory
            process_directory_simple(base_path, new_relative_path, excludes, binary_handling,
                                     symlink_handling, output, inode_tracker, show_size, level + 1, total_size);
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

            stream_output_write(output, structure_line, strlen(structure_line));
            *total_size += statbuf.st_size;
        }
    }

    closedir(dir);
#endif
}

// Enhanced file concatenation with symlink handling
static void concat_files_simple(const char *base_path, const char *current_path,
                                ExcludeList *excludes, BinaryHandling binary_handling,
                                SymlinkHandling symlink_handling, StreamingOutput *output,
                                InodeTracker *inode_tracker)
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
        char new_full_path[MAX_PATH];

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

        if (safe_path_join(new_full_path, sizeof(new_full_path), base_path, new_relative_path) < 0)
        {
            free(utf8_filename);
            continue;
        }

        if (is_excluded(new_relative_path, excludes))
        {
            free(utf8_filename);
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            concat_files_simple(base_path, new_relative_path, excludes, binary_handling,
                                symlink_handling, output, inode_tracker);
        }
        else
        {
            // Process file
            int is_binary = is_binary_file(new_full_path);
            if (is_binary == 1)
            {
                if (binary_handling == BINARY_SKIP)
                {
                    free(utf8_filename);
                    continue;
                }
                else if (binary_handling == BINARY_PLACEHOLDER)
                {
                    char placeholder[MAX_PATH + 100];
                    snprintf(placeholder, sizeof(placeholder),
                             "// File: %s\n// [Binary file - content not displayed]\n\n",
                             new_relative_path);
                    stream_output_write(output, placeholder, strlen(placeholder));
                    free(utf8_filename);
                    continue;
                }
            }

            FILE *file = fopen(new_full_path, "rb");
            if (file)
            {
                char header[MAX_PATH + 50];
                snprintf(header, sizeof(header), "// File: %s\n", new_relative_path);
                stream_output_write(output, header, strlen(header));

                char buffer[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                {
                    stream_output_write(output, buffer, bytes_read);
                }

                stream_output_write(output, "\n\n", 2);
                fclose(file);
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
                    char placeholder[MAX_PATH + 100];
                    snprintf(placeholder, sizeof(placeholder),
                             "// File: %s\n// [Broken symlink - target not accessible]\n\n",
                             new_relative_path);
                    stream_output_write(output, placeholder, strlen(placeholder));
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
                    concat_files_simple(base_path, new_relative_path, excludes, binary_handling,
                                        symlink_handling, output, inode_tracker);
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
                            char placeholder[MAX_PATH + 100];
                            snprintf(placeholder, sizeof(placeholder),
                                     "// File: %s\n// [Binary symlink file - content not displayed]\n\n",
                                     new_relative_path);
                            stream_output_write(output, placeholder, strlen(placeholder));
                            continue;
                        }
                    }

                    FILE *file = fopen(new_full_path, "rb");
                    if (file)
                    {
                        char header[MAX_PATH + 50];
                        snprintf(header, sizeof(header), "// File: %s (symlink)\n", new_relative_path);
                        stream_output_write(output, header, strlen(header));

                        char buffer[BUFFER_SIZE];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                        {
                            stream_output_write(output, buffer, bytes_read);
                        }

                        stream_output_write(output, "\n\n", 2);
                        fclose(file);
                    }
                }
            }
            else if (symlink_handling == SYMLINK_PLACEHOLDER)
            {
                char placeholder[MAX_PATH + 100];
                snprintf(placeholder, sizeof(placeholder),
                         "// File: %s\n// [Symlink - content not followed]\n\n",
                         new_relative_path);
                stream_output_write(output, placeholder, strlen(placeholder));
            }
        }
        else if (S_ISDIR(statbuf.st_mode))
        {
            concat_files_simple(base_path, new_relative_path, excludes, binary_handling,
                                symlink_handling, output, inode_tracker);
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
                    char placeholder[MAX_PATH + 100];
                    snprintf(placeholder, sizeof(placeholder),
                             "// File: %s\n// [Binary file - content not displayed]\n\n",
                             new_relative_path);
                    stream_output_write(output, placeholder, strlen(placeholder));
                    continue;
                }
            }

            FILE *file = fopen(new_full_path, "rb");
            if (file)
            {
                char header[MAX_PATH + 50];
                snprintf(header, sizeof(header), "// File: %s\n", new_relative_path);
                stream_output_write(output, header, strlen(header));

                char buffer[BUFFER_SIZE];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
                {
                    stream_output_write(output, buffer, bytes_read);
                }

                stream_output_write(output, "\n\n", 2);
                fclose(file);
            }
        }
    }

    closedir(dir);
#endif
}

int process_directory_threaded(ProcessingContext *ctx)
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
    const char *structure_header = "Directory Structure:\n==================\n\n";
    stream_output_write(ctx->output, structure_header, strlen(structure_header));

    // For now, use simplified single-threaded approach to avoid deadlocks
    unsigned long long total_size = 0;
    process_directory_simple(ctx->base_path, "", ctx->excludes, ctx->binary_handling,
                             ctx->symlink_handling, ctx->output, &inode_tracker, ctx->show_size, 0, &total_size);

    // Write total size if requested
    if (ctx->show_size)
    {
        char total_line[100];
        char size_buf[32];
        format_size(total_size, size_buf, sizeof(size_buf));
        snprintf(total_line, sizeof(total_line), "\nTotal Size: %s (%llu bytes)\n", size_buf, total_size);
        stream_output_write(ctx->output, total_line, strlen(total_line));
    }

    // Write file contents header
    const char *contents_header = "\nFile Contents:\n=============\n\n";
    stream_output_write(ctx->output, contents_header, strlen(contents_header));

    // Reset inode tracker for file concatenation
    free_inode_tracker(&inode_tracker);
    if (init_inode_tracker(&inode_tracker) != 0)
    {
        fprintf(stderr, "Error reinitializing inode tracker\n");
        return -1;
    }

    // Concatenate files
    concat_files_simple(ctx->base_path, "", ctx->excludes, ctx->binary_handling,
                        ctx->symlink_handling, ctx->output, &inode_tracker);

    // Cleanup inode tracker
    free_inode_tracker(&inode_tracker);

    if (is_verbose())
        fprintf(stderr, "[fconcat] Directory processing complete\n");

    return 0;
}

void print_processing_stats(ThreadPool *pool)
{
    printf("\nProcessing Statistics:\n");
    printf("=====================\n");
    printf("Files processed:       %lu\n", atomic_load(&pool->files_processed));
    printf("Files skipped:         %lu\n", atomic_load(&pool->files_skipped));
    printf("Directories processed: %lu\n", atomic_load(&pool->directories_processed));
    printf("Symlinks processed:    %lu\n", atomic_load(&pool->symlinks_processed));
    printf("Symlinks skipped:      %lu\n", atomic_load(&pool->symlinks_skipped));
    printf("Bytes processed:       %lu\n", atomic_load(&pool->bytes_processed));

    char size_buf[32];
    format_size(atomic_load(&pool->bytes_processed), size_buf, sizeof(size_buf));
    printf("Total data processed:  %s\n", size_buf);
}