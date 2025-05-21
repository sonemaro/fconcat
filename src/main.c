#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "concat.h"

#define FCONCAT_VERSION "1.0.0"
#define FCONCAT_COPYRIGHT "Copyright (c) 2025 Soroush Khosravi Dehaghi"

static int is_verbose() {
    const char *env = getenv("FCONCAT_VERBOSE");
    return env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0);
}

// Check if a file path is within a directory
static int is_path_in_directory(const char *file_path, const char *dir_path) {
    char abs_file[PATH_MAX];
    char abs_dir[PATH_MAX];
    
    // Get absolute paths
    if (!realpath(file_path, abs_file) || !realpath(dir_path, abs_dir)) {
        return 0; // Error getting real paths, assume not in directory
    }
    
    // Check if the file path starts with the directory path
    size_t dir_len = strlen(abs_dir);
    if (strncmp(abs_file, abs_dir, dir_len) == 0) {
        // Make sure it's a proper subdirectory
        // Either exact match or directory path ends with separator
        if (abs_dir[dir_len - 1] == PATH_SEP || abs_file[dir_len] == PATH_SEP) {
            return 1; // File is in the directory
        }
    }
    
    return 0;
}

// Get relative path from base_dir to target_path
static char* get_relative_path(const char *base_dir, const char *target_path) {
    char abs_base[PATH_MAX];
    char abs_target[PATH_MAX];
    
    // Get absolute paths
    if (!realpath(base_dir, abs_base) || !realpath(target_path, abs_target)) {
        return NULL; // Error getting real paths
    }
    
    size_t base_len = strlen(abs_base);
    
    // Ensure base path ends with separator
    if (abs_base[base_len - 1] != PATH_SEP) {
        abs_base[base_len] = PATH_SEP;
        abs_base[base_len + 1] = '\0';
        base_len++;
    }
    
    // Check if target is inside base
    if (strncmp(abs_target, abs_base, base_len) == 0) {
        return strdup(abs_target + base_len);
    }
    
    return NULL;
}

void print_header() {
    printf("fconcat v%s - Concatenate directory structure and files\n", FCONCAT_VERSION);
    printf("%s\n", FCONCAT_COPYRIGHT);
    printf("======================================================\n\n");
}

void print_usage(const char *program_name) {
    fprintf(stderr,
        "Usage:\n"
        "  %s <input_directory> <output_file> [--exclude pattern1 pattern2 ...] [--show-size]\n"
        "\n"
        "Description:\n"
        "  fconcat recursively scans <input_directory>, writes a tree view of its structure,\n"
        "  and concatenates the contents of all files into <output_file>.\n"
        "\n"
        "Options:\n"
        "  <input_directory>      Path to the directory to scan and concatenate.\n"
        "  <output_file>         Path to the output file to write results.\n"
        "  --exclude <patterns>  (Optional) Exclude files/directories matching any of the given patterns.\n"
        "                        Patterns support wildcards '*' (any sequence) and '?' (single char).\n"
        "                        Multiple patterns can be specified after --exclude.\n"
        "  --show-size, -s       (Optional) Display file sizes in the directory structure and total size.\n"
        "\n"
        "Environment:\n"
        "  FCONCAT_VERBOSE=1     Enable verbose logging to stderr for debugging.\n"
        "\n"
        "Examples:\n"
        "  %s ./src all.txt\n"
        "  %s ./project result.txt --exclude \"*.log\" \"build/*\" \"temp?.txt\"\n"
        "\n"
        "Exit Codes:\n"
        "  0   Success\n"
        "  1   Error (see message)\n"
        "\n"
        "For more information, visit: https://github.com/sonemaro/fconcat\n",
        program_name, program_name, program_name
    );
}

int main(int argc, char *argv[]) {
    print_header();

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_dir = argv[1];
    const char *output_file = argv[2];
    if (!input_dir || !output_file || strlen(input_dir) == 0 || strlen(output_file) == 0) {
        fprintf(stderr, "Error: Input directory and output file must be specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    ExcludeList excludes;
    init_exclude_list(&excludes);

    // Parse command line options
    int exclude_count = 0;
    int show_size = 0;  // Flag to show file sizes
    
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--exclude") == 0) {
            i++;
            while (i < argc && argv[i][0] != '-') {
                if (is_verbose()) fprintf(stderr, "[fconcat] Adding exclude pattern: %s\n", argv[i]);
                add_exclude_pattern(&excludes, argv[i]);
                exclude_count++;
                i++;
            }
            i--;
        }
        else if (strcmp(argv[i], "--show-size") == 0 || strcmp(argv[i], "-s") == 0) {
            show_size = 1;
            if (is_verbose()) fprintf(stderr, "[fconcat] File size display enabled\n");
        }
    }

    // Auto-exclude the output file if it's in the input directory
    if (is_path_in_directory(output_file, input_dir)) {
        char *relative_path = get_relative_path(input_dir, output_file);
        
        if (relative_path) {
            if (is_verbose()) fprintf(stderr, "[fconcat] Output file is inside input directory. Auto-excluding: %s\n", relative_path);
            add_exclude_pattern(&excludes, relative_path);
            exclude_count++;
            free(relative_path);
        } else {
            // Fallback to basename if relative path cannot be determined
            char *output_basename = strrchr(output_file, PATH_SEP);
            if (output_basename) {
                output_basename++; // Skip the separator
            } else {
                output_basename = (char*)output_file; // No separator, use the whole path
            }
            
            if (is_verbose()) fprintf(stderr, "[fconcat] Output file is inside input directory. Auto-excluding: %s\n", output_basename);
            add_exclude_pattern(&excludes, output_basename);
            exclude_count++;
        }
    }

    printf("Input directory : %s\n", input_dir);
    printf("Output file     : %s\n", output_file);
    if (exclude_count > 0) {
        printf("Exclude patterns: ");
        for (int i = 0; i < excludes.count; ++i) {
            printf("%s%s", excludes.patterns[i], (i < excludes.count - 1) ? ", " : "");
        }
        printf("\n");
    }
    printf("\n");

    FILE *output = fopen(output_file, "wb");
    if (!output) {
        fprintf(stderr, "Error opening output file '%s': %s\n", output_file, strerror(errno));
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }

    printf("Scanning directory structure...\n");
    if (is_verbose()) fprintf(stderr, "[fconcat] Writing directory structure...\n");
    if (fprintf(output, "Directory Structure:\n==================\n\n") < 0) {
        fprintf(stderr, "Error writing to output file.\n");
        fclose(output);
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }
    
    unsigned long long total_size = 0;
    list_files(output, input_dir, "", 0, &excludes, show_size, &total_size);
    
    // Display total size if requested
    if (show_size) {
        char size_buf[32];
        format_size(total_size, size_buf, sizeof(size_buf));
        fprintf(output, "\nTotal Size: %s (%llu bytes)\n", size_buf, total_size);
    }

    printf("Concatenating file contents...\n");
    if (is_verbose()) fprintf(stderr, "[fconcat] Writing file contents...\n");
    if (fprintf(output, "\nFile Contents:\n=============\n\n") < 0) {
        fprintf(stderr, "Error writing to output file.\n");
        fclose(output);
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }
    concat_files(output, input_dir, "", &excludes);

    if (fclose(output) != 0) {
        fprintf(stderr, "Error closing output file: %s\n", strerror(errno));
        free_exclude_list(&excludes);
        return EXIT_FAILURE;
    }
    free_exclude_list(&excludes);

    printf("\nDone! Output written to '%s'.\n", output_file);
    printf("Thank you for using fconcat.\n");
    if (is_verbose()) fprintf(stderr, "[fconcat] Done.\n");
    return EXIT_SUCCESS;
}

