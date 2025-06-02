#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "concat.h"

#define FCONCAT_VERSION "1.1.0"
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
    if (realpath(path, abs_path))
    {
        return abs_path;
    }

    // If realpath fails (e.g., file doesn't exist yet), copy the path as is
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

    size_t base_len = strlen(abs_base);

    // Ensure base path ends with separator
    if (base_len > 0 && abs_base[base_len - 1] != PATH_SEP)
    {
        abs_base[base_len] = PATH_SEP;
        abs_base[base_len + 1] = '\0';
        base_len++;
    }

    // Check if target is inside base
    if (strncmp(abs_target, abs_base, base_len) == 0)
    {
        return strdup(abs_target + base_len);
    }

    return NULL;
}

void print_header()
{
    printf("fconcat v%s - Concatenate directory structure and files\n", FCONCAT_VERSION);
    printf("%s\n", FCONCAT_COPYRIGHT);
    printf("======================================================\n\n");
}

void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s <input_directory> <output_file> [options]\n"
            "\n"
            "Description:\n"
            "  fconcat recursively scans <input_directory>, writes a tree view of its structure,\n"
            "  and concatenates the contents of all files into <output_file>.\n"
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
            "\n"
            "Environment:\n"
            "  FCONCAT_VERBOSE=1     Enable verbose logging to stderr for debugging.\n"
            "\n"
            "Examples:\n"
            "  %s ./src all.txt\n"
            "  %s ./project result.txt --exclude \"*.log\" \"build/*\" \"temp?.txt\"\n"
            "  %s ./code output.txt --show-size --binary-placeholder\n"
            "\n"
            "Binary File Handling:\n"
            "  By default, binary files are skipped. Use --binary-include to include them\n"
            "  or --binary-placeholder to show a placeholder instead of content.\n"
            "\n"
            "Exit Codes:\n"
            "  0   Success\n"
            "  1   Error (see message)\n"
            "\n"
            "For more information, visit: https://github.com/sonemaro/fconcat\n",
            program_name, program_name, program_name, program_name);
}

int main(int argc, char *argv[])
{
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
    BinaryHandling binary_handling = BINARY_SKIP; // Default behavior

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
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            free_exclude_list(&excludes);
            return EXIT_FAILURE;
        }
    }

    // Always exclude the output file to prevent infinite loops

    // 1. Exclude by filename
    const char *output_basename = get_filename(output_file);
    if (is_verbose())
        fprintf(stderr, "[fconcat] Auto-excluding output file by name: %s\n", output_basename);
    add_exclude_pattern(&excludes, output_basename);
    exclude_count++;

    // 2. For current directory case, exclude by exact path
    if (strcmp(input_dir, ".") == 0)
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Auto-excluding output file by path (current dir): %s\n", output_file);
        add_exclude_pattern(&excludes, output_file);
        exclude_count++;
    }

    // 3. If output is in input directory, exclude by relative path
    char *relative_path = get_relative_path(input_dir, output_file);
    if (relative_path)
    {
        if (is_verbose())
            fprintf(stderr, "[fconcat] Auto-excluding output file by relative path: %s\n", relative_path);
        add_exclude_pattern(&excludes, relative_path);
        exclude_count++;
        free(relative_path);
    }

    printf("Input directory : %s\n", input_dir);
    printf("Output file     : %s\n", output_file);
    printf("Binary handling : %s\n",
           binary_handling == BINARY_SKIP ? "skip" : binary_handling == BINARY_INCLUDE ? "include"
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

    printf("Scanning directory structure...\n");
    if (is_verbose())
        fprintf(stderr, "[fconcat] Writing directory structure...\n");
    if (fprintf(output, "Directory Structure:\n==================\n\n") < 0)
    {
        fprintf(stderr, "Error writing to output file.\n");
        fclose(output);
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }

    unsigned long long total_size = 0;
    list_files(output, input_dir, "", 0, &excludes, show_size, &total_size);

    // Display total size if requested
    if (show_size)
    {
        char size_buf[32];
        format_size(total_size, size_buf, sizeof(size_buf));
        fprintf(output, "\nTotal Size: %s (%llu bytes)\n", size_buf, total_size);
    }

    printf("Concatenating file contents...\n");
    if (is_verbose())
        fprintf(stderr, "[fconcat] Writing file contents...\n");
    if (fprintf(output, "\nFile Contents:\n=============\n\n") < 0)
    {
        fprintf(stderr, "Error writing to output file.\n");
        fclose(output);
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }
    concat_files(output, input_dir, "", &excludes, binary_handling);

    if (fclose(output) != 0)
    {
        fprintf(stderr, "Error closing output file: %s\n", strerror(errno));
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }
    free_exclude_list(&excludes);

    printf("\nDone! Output written to '%s'.\n", output_file);
    printf("Thank you for using fconcat.\n");
    if (is_verbose())
        fprintf(stderr, "[fconcat] Done.\n");
    return EXIT_SUCCESS;
}