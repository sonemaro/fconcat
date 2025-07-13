#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#include <libgen.h>  // For basename on MinGW
#define PATH_MAX 260
// Case-insensitive string comparison for Windows
#define strnicmp _strnicmp
#else
#include <limits.h>
#endif

#include "concat.h"

#define FCONCAT_VERSION "0.0.1"
#define FCONCAT_COPYRIGHT "Copyright (c) 2025 Soroush Khosravi Dehaghi"

static int is_verbose()
{
    const char *env = getenv("FCONCAT_VERBOSE");
    return env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
}

// Get the basename (filename) part of a path
static char *get_filename(const char *path)
{
    const char *basename = strrchr(path, PATH_SEP);
    if (basename)
    {
        return (char *)(basename + 1);
    }
    return (char *)path;
}

// Get absolute path
static char *get_absolute_path(const char *path, char *abs_path, size_t abs_path_size)
{
#ifdef _WIN32
    // Use GetFullPathName on Windows
    DWORD result = GetFullPathNameA(path, (DWORD)abs_path_size, abs_path, NULL);
    if (result > 0 && result < abs_path_size) {
        return abs_path;
    }
#else
    // Use realpath on Unix-like systems
    if (realpath(path, abs_path)) {
        return abs_path;
    }
#endif

    // Fallback: just copy the path as-is
    strncpy(abs_path, path, abs_path_size - 1);
    abs_path[abs_path_size - 1] = '\0';
    return abs_path;
}

// Get relative path from base_dir to target_path
static char *get_relative_path(const char *base_dir, const char *target_path)
{
    char abs_base[PATH_MAX];
    char abs_target[PATH_MAX];

    get_absolute_path(base_dir, abs_base, sizeof(abs_base));
    get_absolute_path(target_path, abs_target, sizeof(abs_target));

#ifdef _WIN32
    // Normalize paths to use forward slashes for consistency
    for (char *p = abs_base; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    for (char *p = abs_target; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    
    // Windows path comparison should be case-insensitive
    #define PATH_COMPARE strnicmp
    #define PATH_COMPARE_N strnicmp
#else
    #define PATH_COMPARE strcmp
    #define PATH_COMPARE_N strncmp
#endif

    size_t base_len = strlen(abs_base);

    if (base_len > 0 && abs_base[base_len - 1] != '/' && abs_base[base_len - 1] != PATH_SEP)
    {
        abs_base[base_len] = '/';
        abs_base[base_len + 1] = '\0';
        base_len++;
    }

    if (PATH_COMPARE_N(abs_target, abs_base, base_len) == 0)
    {
        return strdup(abs_target + base_len);
    }

    return NULL;
}

void print_header()
{
    printf("fconcat v%s - Multi-threaded file concatenator with streaming output\n", FCONCAT_VERSION);
    printf("%s\n", FCONCAT_COPYRIGHT);
    printf("==================================================================\n\n");
}

void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s <input_directory> <output_file> [options]\n"
            "\n"
            "Description:\n"
            "  fconcat recursively scans <input_directory>, writes a tree view of its structure,\n"
            "  and concatenates the contents of all files into <output_file> using multi-threading\n"
            "  and streaming output for optimal performance.\n"
            "\n"
            "Options:\n"
            "  <input_directory>     Path to the directory to scan and concatenate.\n"
            "  <output_file>         Path to the output file to write results.\n"
            "  --exclude <patterns>  Exclude files/directories matching any of the given patterns.\n"
            "                        Patterns support wildcards '*' (any sequence) and '?' (single char).\n"
            "                        Multiple patterns can be specified after --exclude.\n"
            "  --show-size, -s       Display file sizes in the directory structure and total size.\n"
            "  --binary-skip         Skip binary files entirely (default behavior).\n"
            "  --binary-include      Include binary files in concatenation.\n"
            "  --binary-placeholder  Show placeholder for binary files instead of content.\n"
            "  --symlinks <mode>     How to handle symbolic links:\n"
            "                        skip        - Skip all symlinks (default, safe)\n"
            "                        follow      - Follow symlinks with loop detection\n"
            "                        include     - Include symlink targets as files\n"
            "                        placeholder - Show symlinks as placeholders\n"
            "  --threads <n>, -t <n> Number of worker threads (1-%d, default: %d).\n"
            "\n"
            "Environment:\n"
            "  FCONCAT_VERBOSE=1     Enable verbose logging to stderr for debugging.\n"
            "\n"
            "Examples:\n"
            "  %s ./src all.txt\n"
            "  %s ./project result.txt --exclude \"*.log\" \"build/*\" \"temp?.txt\"\n"
            "  %s ./code output.txt --show-size --binary-placeholder --threads 8\n"
            "  %s ./kernel out.txt --symlinks follow --exclude \"*.o\" \"*.ko\"\n"
            "\n"
            "Symbolic Link Handling:\n"
            "  skip        - Safest option, ignores all symbolic links\n"
            "  follow      - Follows symlinks but detects and prevents infinite loops\n"
            "  include     - Includes symlink targets as regular files (no recursion)\n"
            "  placeholder - Shows symlinks in structure but doesn't follow them\n"
            "\n"
            "Performance Features:\n"
            "  - Multi-threaded directory traversal and file processing\n"
            "  - Streaming output for constant memory usage\n"
            "  - Hash-table based exclude pattern matching\n"
            "  - Platform-optimized Unicode filename support\n"
            "  - Intelligent binary file detection\n"
            "  - Robust symbolic link handling with loop detection\n"
            "\n"
            "Exit Codes:\n"
            "  0   Success\n"
            "  1   Error (see message)\n"
            "\n"
            "For more information, visit: https://github.com/sonemaro/fconcat\n",
            program_name, MAX_THREADS, DEFAULT_WORKER_THREADS,
            program_name, program_name, program_name, program_name);
}

int main(int argc, char *argv[])
{
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    print_header();

    if (argc < 3)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_dir = argv[1];
    const char *output_file = argv[2];
    if (!input_dir || !output_file || strlen(input_dir) == 0 || strlen(output_file) == 0)
    {
        fprintf(stderr, "Error: Input directory and output file must be specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Get absolute paths for comparison
    char abs_input[PATH_MAX];
    char abs_output[PATH_MAX];
    get_absolute_path(input_dir, abs_input, sizeof(abs_input));
    get_absolute_path(output_file, abs_output, sizeof(abs_output));

    ExcludeList excludes;
    init_exclude_list(&excludes);

    // Parse command line options
    int exclude_count = 0;
    int show_size = 0;
    int num_threads = DEFAULT_WORKER_THREADS;
    BinaryHandling binary_handling = BINARY_SKIP;
    SymlinkHandling symlink_handling = SYMLINK_SKIP;

    for (int i = 3; i < argc; i++)
    {
        if (strcmp(argv[i], "--exclude") == 0)
        {
            i++;
            while (i < argc && argv[i][0] != '-')
            {
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Adding exclude pattern: %s\n", argv[i]);
                add_exclude_pattern(&excludes, argv[i]);
                exclude_count++;
                i++;
            }
            i--;
        }
        else if (strcmp(argv[i], "--show-size") == 0 || strcmp(argv[i], "-s") == 0)
        {
            show_size = 1;
            if (is_verbose())
                fprintf(stderr, "[fconcat] File size display enabled\n");
        }
        else if (strcmp(argv[i], "--threads") == 0 || strcmp(argv[i], "-t") == 0)
        {
            if (i + 1 < argc)
            {
                num_threads = atoi(argv[++i]);
                if (num_threads < 1 || num_threads > MAX_THREADS)
                {
                    fprintf(stderr, "Error: Number of threads must be between 1 and %d\n", MAX_THREADS);
                    free_exclude_list(&excludes);
                    return EXIT_FAILURE;
                }
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Using %d worker threads\n", num_threads);
            }
            else
            {
                fprintf(stderr, "Error: --threads requires a number\n");
                free_exclude_list(&excludes);
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--binary-skip") == 0)
        {
            binary_handling = BINARY_SKIP;
            if (is_verbose())
                fprintf(stderr, "[fconcat] Binary handling: skip\n");
        }
        else if (strcmp(argv[i], "--binary-include") == 0)
        {
            binary_handling = BINARY_INCLUDE;
            if (is_verbose())
                fprintf(stderr, "[fconcat] Binary handling: include\n");
        }
        else if (strcmp(argv[i], "--binary-placeholder") == 0)
        {
            binary_handling = BINARY_PLACEHOLDER;
            if (is_verbose())
                fprintf(stderr, "[fconcat] Binary handling: placeholder\n");
        }
        else if (strcmp(argv[i], "--symlinks") == 0)
        {
            if (i + 1 < argc)
            {
                i++;
                if (strcmp(argv[i], "skip") == 0)
                {
                    symlink_handling = SYMLINK_SKIP;
                }
                else if (strcmp(argv[i], "follow") == 0)
                {
                    symlink_handling = SYMLINK_FOLLOW;
                }
                else if (strcmp(argv[i], "include") == 0)
                {
                    symlink_handling = SYMLINK_INCLUDE;
                }
                else if (strcmp(argv[i], "placeholder") == 0)
                {
                    symlink_handling = SYMLINK_PLACEHOLDER;
                }
                else
                {
                    fprintf(stderr, "Error: Invalid symlink mode '%s'. Use: skip, follow, include, or placeholder\n", argv[i]);
                    free_exclude_list(&excludes);
                    return EXIT_FAILURE;
                }
                if (is_verbose())
                    fprintf(stderr, "[fconcat] Symlink handling: %s\n", argv[i]);
            }
            else
            {
                fprintf(stderr, "Error: --symlinks requires a mode (skip, follow, include, placeholder)\n");
                free_exclude_list(&excludes);
                return EXIT_FAILURE;
            }
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            free_exclude_list(&excludes);
            return EXIT_FAILURE;
        }
    }

    // Auto-exclude output file with better path resolution
    // Reuse existing abs_input and abs_output variables
    
    // Check if output file is inside input directory
    int output_inside_input = 0;
    
#ifdef _WIN32
    // Normalize paths for comparison
    for (char *p = abs_input; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    for (char *p = abs_output; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    // Windows case-insensitive comparison
    if (strnicmp(abs_output, abs_input, strlen(abs_input)) == 0) {
        output_inside_input = 1;
    }
#else
    if (strncmp(abs_output, abs_input, strlen(abs_input)) == 0) {
        output_inside_input = 1;
    }
#endif

    if (output_inside_input) {
        // Add absolute path exclusion
        if (is_verbose())
            fprintf(stderr, "[fconcat] Auto-excluding output file by absolute path: %s\n", abs_output);
        add_exclude_pattern(&excludes, abs_output);
        exclude_count++;
        
        // Add relative path exclusion
        char *relative_path = get_relative_path(input_dir, output_file);
        if (relative_path) {
            if (is_verbose())
                fprintf(stderr, "[fconcat] Auto-excluding output file by relative path: %s\n", relative_path);
            add_exclude_pattern(&excludes, relative_path);
            exclude_count++;
            free(relative_path);
        }
    }
    
    // Always exclude by basename as fallback
    const char *output_basename = get_filename(output_file);
    if (is_verbose())
        fprintf(stderr, "[fconcat] Auto-excluding output file by name: %s\n", output_basename);
    add_exclude_pattern(&excludes, output_basename);
    exclude_count++;

    // Special case for current directory
    if (strcmp(input_dir, ".") == 0)
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Auto-excluding output file by path (current dir): %s\n", output_file);
        add_exclude_pattern(&excludes, output_file);
        exclude_count++;
    }

    printf("Input directory : %s\n", input_dir);
    printf("Output file     : %s\n", output_file);
    printf("Worker threads  : %d\n", num_threads);
    printf("Binary handling : %s\n",
           binary_handling == BINARY_SKIP ? "skip" : binary_handling == BINARY_INCLUDE ? "include"
                                                                                       : "placeholder");
    printf("Symlink handling: %s\n",
           symlink_handling == SYMLINK_SKIP ? "skip" : symlink_handling == SYMLINK_FOLLOW ? "follow"
                                                   : symlink_handling == SYMLINK_INCLUDE  ? "include"
                                                                                          : "placeholder");
    if (exclude_count > 0)
    {
        printf("Exclude patterns: %d patterns loaded\n", exclude_count);
    }
    printf("\n");

    FILE *output = fopen(output_file, "wb");
    if (!output)
    {
        fprintf(stderr, "Error opening output file '%s': %s\n", output_file, strerror(errno));
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }

    // Initialize streaming output
    StreamingOutput streaming_output;
    if (init_streaming_output(&streaming_output, output) != 0)
    {
        fprintf(stderr, "Error initializing streaming output\n");
        fclose(output);
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }

    printf("🚀 Processing directory with %d threads...\n", num_threads);
    if (is_verbose())
        fprintf(stderr, "[fconcat] Starting multi-threaded processing...\n");

    // Create processing context
    ProcessingContext ctx = {
        .base_path = input_dir,
        .excludes = &excludes,
        .binary_handling = binary_handling,
        .symlink_handling = symlink_handling,
        .show_size = show_size,
        .num_threads = num_threads,
        .output = &streaming_output,
        .pool = NULL};

    // Process directory with multi-threading
    int result = process_directory_threaded(&ctx);

    if (result == 0)
    {
        printf("✅ Directory structure processed successfully\n");
        printf("📝 Finalizing output...\n");

        // Signal that processing is complete
        stream_output_finish(&streaming_output);

        // Print statistics if verbose
        if (ctx.pool && is_verbose())
        {
            print_processing_stats(ctx.pool);
        }
    }
    else
    {
        fprintf(stderr, "❌ Error during processing\n");
    }

    // Cleanup
    destroy_streaming_output(&streaming_output);

    if (fclose(output) != 0)
    {
        fprintf(stderr, "Error closing output file: %s\n", strerror(errno));
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }

    free_exclude_list(&excludes);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    if (result == 0)
    {
        printf("\n🎉 Success! Output written to '%s'\n", output_file);
        printf("⏱️  Processing time: %.3f seconds\n", elapsed);

        if (ctx.pool)
        {
            unsigned long files = atomic_load(&ctx.pool->files_processed);
            unsigned long bytes = atomic_load(&ctx.pool->bytes_processed);
            unsigned long symlinks = atomic_load(&ctx.pool->symlinks_processed);

            if (files > 0)
            {
                printf("📊 Performance: %.0f files/sec, %.1f MB/sec\n",
                       files / elapsed, (bytes / elapsed) / (1024 * 1024));
            }
            if (symlinks > 0)
            {
                printf("🔗 Symlinks: %lu processed\n", symlinks);
            }
        }

        printf("Thank you for using fconcat! 🚀\n");
        if (is_verbose())
            fprintf(stderr, "[fconcat] Done.\n");
        return EXIT_SUCCESS;
    }
    else
    {
        return EXIT_FAILURE;
    }
}