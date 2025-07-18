/**
 * @file remove_main.c
 * @brief fconcat plugin to remove main() functions from C/C++ source files
 * @version 1.0.0
 * @author fconcat project
 * 
 * This plugin processes C/C++ source files and removes main() function definitions
 * while preserving all other code. It's useful for concatenating source files
 * where multiple main() functions would cause compilation conflicts.
 * 
 * Features:
 * - Supports C (.c) and C++ (.cpp, .cc, .cxx) files
 * - Handles string literals and comments correctly (won't remove "main" inside them)
 * - Streaming processing with chunk boundary handling
 * - Memory efficient with 300-byte carry-over buffer
 * - Replaces removed functions with descriptive comments
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Plugin API structures
typedef struct
{
    void *private_data;
    const char *file_path;
    size_t total_processed;
    int plugin_index;
} PluginContext;

typedef struct
{
    const char *name;
    const char *version;
    int (*init)(void);
    void (*cleanup)(void);
    PluginContext *(*file_start)(const char *relative_path);
    int (*process_chunk)(PluginContext *ctx, const char *input, size_t input_size,
                         char **output, size_t *output_size);
    int (*file_end)(PluginContext *ctx, char **final_output, size_t *final_size);
    void (*file_cleanup)(PluginContext *ctx);
} StreamingPlugin;

// Plugin state structure
typedef struct
{
    bool in_main_function;        // Currently inside a main function
    bool in_string;               // Currently inside a string literal
    bool in_comment;              // Currently inside a /* */ comment
    bool in_single_comment;       // Currently inside a // comment
    bool main_found;              // Whether we found and removed a main function
    char quote_char;              // Type of quote (' or ") for string tracking
    int brace_count;              // Current brace nesting level
    int main_start_brace_level;   // Brace level when main function started
    char *carry_over;             // Buffer for chunk boundary handling
    size_t carry_over_size;       // Size of carry-over buffer
    bool is_c_file;               // Whether this is a C/C++ file
} RemoveMainState;

// Carry-over buffer size for handling function detection across chunk boundaries
#define CARRY_OVER_SIZE 300

static int remove_main_init(void)
{
    return 0;
}

static void remove_main_cleanup(void)
{
}

/**
 * @brief Check if a file is a C/C++ source file based on extension
 * @param filename File name to check
 * @return true if it's a C/C++ file, false otherwise
 */
static bool is_c_file(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return false;
    return (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cc") == 0 || strcmp(ext, ".cxx") == 0);
}

/**
 * @brief Initialize plugin state for a new file
 * @param relative_path Path to the file being processed
 * @return Plugin context or NULL on error
 */
static PluginContext *remove_main_file_start(const char *relative_path)
{
    PluginContext *ctx = malloc(sizeof(PluginContext));
    if (!ctx)
        return NULL;

    RemoveMainState *state = malloc(sizeof(RemoveMainState));
    if (!state)
    {
        free(ctx);
        return NULL;
    }

    // Initialize state
    memset(state, 0, sizeof(RemoveMainState));
    state->in_main_function = false;
    state->in_string = false;
    state->in_comment = false;
    state->in_single_comment = false;
    state->main_found = false;
    state->quote_char = 0;
    state->brace_count = 0;
    state->main_start_brace_level = 0;
    state->carry_over = NULL;
    state->carry_over_size = 0;
    state->is_c_file = is_c_file(relative_path);

    ctx->private_data = state;
    ctx->file_path = strdup(relative_path);
    ctx->total_processed = 0;
    ctx->plugin_index = 0;

    return ctx;
}

/**
 * @brief Detect if current position in text is start of a main function
 * @param text Input text buffer
 * @param pos Current position in buffer
 * @param len Total length of buffer
 * @return true if main function starts at this position
 */
static bool is_main_function_start(const char *text, size_t pos, size_t len)
{
    // Look for "main(" pattern
    if (pos + 5 >= len)
        return false;
    
    // Must be "main("
    if (strncmp(text + pos, "main(", 5) != 0)
        return false;

    // Check character before "main" - should be whitespace, not alphanumeric
    if (pos > 0 && isalnum(text[pos - 1]))
        return false;

    // Look backwards for return type keywords within reasonable distance
    int search_start = (pos > 150) ? pos - 150 : 0;
    
    // Scan backwards looking for return type patterns
    for (int i = pos - 1; i >= search_start; i--)
    {
        // Skip whitespace and newlines
        if (isspace(text[i]))
            continue;
            
        // Check for "int" - look at position i-2, i-1, i for "int"
        if (i >= 2 && strncmp(text + i - 2, "int", 3) == 0)
        {
            // Make sure it's a word boundary before "int"
            if (i == 2 || !isalnum(text[i - 3]))
            {
                // Make sure it's a word boundary after "int"
                if ((size_t)(i + 1) >= len || !isalnum(text[i + 1]))
                {
                    return true;
                }
            }
        }
        
        // Check for "void" - look at position i-3, i-2, i-1, i for "void"
        if (i >= 3 && strncmp(text + i - 3, "void", 4) == 0)
        {
            // Make sure it's a word boundary before "void"
            if (i == 3 || !isalnum(text[i - 4]))
            {
                // Make sure it's a word boundary after "void"
                if ((size_t)(i + 1) >= len || !isalnum(text[i + 1]))
                {
                    return true;
                }
            }
        }
        
        // Stop if we hit certain delimiters
        if (text[i] == ';' || text[i] == '}')
            break;
    }

    return false;
}

/**
 * @brief Process a chunk of input data and remove main functions
 * @param ctx Plugin context
 * @param input Input data chunk
 * @param input_size Size of input chunk
 * @param output Pointer to output buffer (allocated by this function)
 * @param output_size Pointer to output size
 * @return 0 on success, -1 on error
 * 
 * This function processes input in chunks while maintaining state across
 * chunk boundaries. It handles:
 * - String literals and comments (to avoid false positives)
 * - Main function detection with proper return type checking
 * - Brace counting for accurate function boundaries
 * - Carry-over buffer for functions spanning multiple chunks
 */
static int remove_main_process_chunk(PluginContext *ctx, const char *input, size_t input_size,
                                     char **output, size_t *output_size)
{
    if (!ctx || !ctx->private_data || !input || input_size == 0)
    {
        *output = NULL;
        *output_size = 0;
        return 0;
    }

    RemoveMainState *state = (RemoveMainState *)ctx->private_data;

    // If not a C file, pass through unchanged
    if (!state->is_c_file)
    {
        *output = malloc(input_size);
        if (!*output)
            return -1;
        memcpy(*output, input, input_size);
        *output_size = input_size;
        return 0;
    }

    // Combine carry over with new input
    size_t total_size = state->carry_over_size + input_size;
    char *combined = malloc(total_size);
    if (!combined)
        return -1;

    if (state->carry_over_size > 0)
    {
        memcpy(combined, state->carry_over, state->carry_over_size);
        free(state->carry_over);
        state->carry_over = NULL;
        state->carry_over_size = 0;
    }
    memcpy(combined + state->carry_over_size, input, input_size);

    // Prepare output buffer - estimate conservatively
    size_t max_output_size = total_size + 1000;  // Add space for potential replacement comment
    char *out = malloc(max_output_size);
    if (!out)
    {
        free(combined);
        return -1;
    }
    size_t out_pos = 0;

    // Process character by character
    for (size_t i = 0; i < total_size && out_pos < max_output_size - 100; i++)
    {
        char c = combined[i];

        // Handle string literals
        if (!state->in_comment && !state->in_single_comment)
        {
            if (c == '"' || c == '\'')
            {
                if (!state->in_string)
                {
                    state->in_string = true;
                    state->quote_char = c;
                }
                else if (c == state->quote_char)
                {
                    // Check if it's escaped
                    int escape_count = 0;
                    for (int j = i - 1; j >= 0 && combined[j] == '\\'; j--)
                    {
                        escape_count++;
                    }
                    if (escape_count % 2 == 0)
                    {
                        state->in_string = false;
                        state->quote_char = 0;
                    }
                }
            }
        }

        // Handle comments
        if (!state->in_string)
        {
            if (c == '/' && i + 1 < total_size)
            {
                if (combined[i + 1] == '*' && !state->in_single_comment)
                {
                    state->in_comment = true;
                }
                else if (combined[i + 1] == '/' && !state->in_comment)
                {
                    state->in_single_comment = true;
                }
            }
            else if (c == '*' && i + 1 < total_size && combined[i + 1] == '/' && state->in_comment)
            {
                state->in_comment = false;
                if (!state->in_main_function)
                {
                    out[out_pos++] = c;
                    out[out_pos++] = combined[++i]; // Add the '/'
                }
                else
                {
                    i++; // Skip the '/'
                }
                continue;
            }
            else if (c == '\n' && state->in_single_comment)
            {
                state->in_single_comment = false;
            }
        }

        // Skip processing if we're in a string or comment
        if (state->in_string || state->in_comment || state->in_single_comment)
        {
            // Add character to output if we're not inside main function
            if (!state->in_main_function)
            {
                out[out_pos++] = c;
            }
            continue;
        }

        // Look for main function
        if (!state->in_main_function && is_main_function_start(combined, i, total_size))
        {
            state->in_main_function = true;
            state->main_found = true;
            state->main_start_brace_level = state->brace_count;

            // Skip to opening brace
            while (i < total_size && combined[i] != '{')
            {
                i++;
            }

            if (i < total_size && combined[i] == '{')
            {
                state->brace_count++;
                state->main_start_brace_level = state->brace_count - 1;
            }
            continue;
        }

        // Handle braces
        if (c == '{')
        {
            state->brace_count++;
        }
        else if (c == '}')
        {
            state->brace_count--;

            // Check if we're exiting the main function
            if (state->in_main_function && state->brace_count == state->main_start_brace_level)
            {
                state->in_main_function = false;
                
                // Add a comment where main function was
                const char *comment = "\n// [main function removed by remove_main plugin]\n";
                size_t comment_len = strlen(comment);
                memcpy(out + out_pos, comment, comment_len);
                out_pos += comment_len;
                continue;
            }
        }

        // Add character to output if we're not inside main function
        if (!state->in_main_function)
        {
            out[out_pos++] = c;
        }
    }

    // Save carry over for next chunk to handle function detection at boundaries
    if (total_size > CARRY_OVER_SIZE)
    {
        free(state->carry_over); // Free previous carry-over if any
        state->carry_over = malloc(CARRY_OVER_SIZE);
        if (state->carry_over)
        {
            memcpy(state->carry_over, combined + total_size - CARRY_OVER_SIZE, CARRY_OVER_SIZE);
            state->carry_over_size = CARRY_OVER_SIZE;
        }
        else
        {
            state->carry_over_size = 0; // Reset on allocation failure
        }
    }
    else
    {
        // If remaining data is smaller than carry-over size, keep it all
        if (total_size > 0)
        {
            free(state->carry_over);
            state->carry_over = malloc(total_size);
            if (state->carry_over)
            {
                memcpy(state->carry_over, combined, total_size);
                state->carry_over_size = total_size;
            }
            else
            {
                state->carry_over_size = 0;
            }
        }
    }

    free(combined);

    // Return the processed output immediately
    *output = out;
    *output_size = out_pos;

    return 0;
}

/**
 * @brief Finalize processing for a file
 * @param ctx Plugin context
 * @param final_output Pointer to final output buffer (not used - returns NULL)
 * @param final_size Pointer to final output size (always 0)
 * @return 0 on success
 * 
 * This function is called after all chunks have been processed.
 * Since we use immediate output in process_chunk(), this function
 * only reports success and doesn't return additional data.
 */
static int remove_main_file_end(PluginContext *ctx, char **final_output, size_t *final_size)
{
    if (!ctx || !ctx->private_data)
    {
        *final_output = NULL;
        *final_size = 0;
        return 0;
    }

    RemoveMainState *state = (RemoveMainState *)ctx->private_data;

    if (state->main_found)
    {
        fprintf(stderr, "✂️  Removed main function from: %s\n", ctx->file_path);
    }

    // Return empty since all processing is done in process_chunk
    *final_output = NULL;
    *final_size = 0;
    return 0;
}

/**
 * @brief Cleanup plugin resources for a file
 * @param ctx Plugin context to clean up
 */
static void remove_main_file_cleanup(PluginContext *ctx)
{
    if (ctx)
    {
        if (ctx->private_data)
        {
            RemoveMainState *state = (RemoveMainState *)ctx->private_data;
            if (state->carry_over)
            {
                free(state->carry_over);
            }
            free(ctx->private_data);
        }
        if (ctx->file_path)
        {
            free((void *)ctx->file_path);
        }
        free(ctx);
    }
}

// Plugin declaration
static StreamingPlugin remove_main_plugin = {
    .name = "Remove Main Function",
    .version = "1.0.0",
    .init = remove_main_init,
    .cleanup = remove_main_cleanup,
    .file_start = remove_main_file_start,
    .process_chunk = remove_main_process_chunk,
    .file_end = remove_main_file_end,
    .file_cleanup = remove_main_file_cleanup
};

/**
 * @brief Plugin entry point - returns plugin interface
 * @return Pointer to plugin structure
 */
StreamingPlugin *get_plugin(void)
{
    return &remove_main_plugin;
}