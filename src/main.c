#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "concat.h"

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <input_directory> <output_file> [--exclude pattern1 pattern2 ...]\n", program_name);
    fprintf(stderr, "Exclude patterns can use wildcards (* and ?)\n");
    fprintf(stderr, "Example: %s /path/to/dir output.txt --exclude \"*.log\" \"temp/*\"\n", program_name);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_dir = argv[1];
    const char *output_file = argv[2];
    ExcludeList excludes;
    init_exclude_list(&excludes);

    // Parse exclude patterns
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--exclude") == 0) {
            i++;
            while (i < argc && argv[i][0] != '-') {
                add_exclude_pattern(&excludes, argv[i]);
                i++;
            }
            i--;
        }
    }

    FILE *output = fopen(output_file, "wb");
    if (!output) {
        perror("Error opening output file");
        free_exclude_list(&excludes);
        return 1;
    }

    fprintf(output, "Directory Structure:\n==================\n\n");
    list_files(output, input_dir, "", 0, &excludes);

    fprintf(output, "\nFile Contents:\n=============\n\n");
    concat_files(output, input_dir, "", &excludes);

    fclose(output);
    free_exclude_list(&excludes);
    return 0;
}

