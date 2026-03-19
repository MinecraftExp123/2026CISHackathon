#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>
#include <ctype.h>

struct macro {
    char *name;
    char *command;
    char *keybinding;
};

enum mode {
    MODE_NORMAL,
    MODE_INSERT_CUSTOM,
    MODE_INSERT_PRESET,
    MODE_RUN_BY_NAME,
    MODE_RUN_BY_KEYBINDING,
    MODE_LIST_MACROS,
    MODE_CLEAR_MACROS,
    MODE_DELETE_MACRO,
    MODE_EDIT_MACRO_BY_NAME,
    MODE_EDIT_MACRO_BY_KEYBIND,
    MODE_IMPORT_CSV,
    MODE_HELP,
};
enum mode mode = MODE_NORMAL;

unsigned short screen_width;

// Clears the input buffer, discarding any pending characters.
// This is used after scanf so that getchar doesn't read trailing newline characters (\n).
void clear_stdin()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

static char *trim(char *str)
{
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Strip surrounding single or double quotes from a string.
// Handles paths like '/path/to/file' or "/path/to/file".
// Modifies the string in-place.
static void strip_quotes(char *str)
{
    size_t len = strlen(str);
    if (len >= 2) {
        if ((str[0] == '\'' && str[len - 1] == '\'') ||
            (str[0] == '"'  && str[len - 1] == '"'))
        {
            memmove(str, str + 1, len - 2);
            str[len - 2] = '\0';
        }
    }
}

// Remove backslash escapes that macOS Terminal adds when you drag-drop
// a file with spaces or special characters (e.g. /path/to/file\ name.csv).
// After this, the path will have literal spaces instead of backslash-space.
static void unescape_backslashes(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '\\' && *(src + 1) != '\0') {
            src++;   // skip the backslash, keep the next char
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

// Clean up a file path: trim whitespace, strip quotes, unescape backslashes.
static void clean_path(char *path)
{
    // Trim trailing whitespace / newlines
    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r' ||
                        path[len - 1] == ' '  || path[len - 1] == '\t'))
        path[--len] = '\0';

    // Trim leading whitespace
    char *start = path;
    while (*start == ' ' || *start == '\t') start++;
    if (start != path) memmove(path, start, strlen(start) + 1);

    // Strip surrounding quotes (handles '/path' or "/path")
    strip_quotes(path);

    // Unescape backslashes from drag-and-drop (handles /path/to/file\ name)
    unescape_backslashes(path);
}

// Print a horizontal line across the terminal width using the given character.
static void print_line(char ch)
{
    for (int i = 0; i < screen_width; i++)
        putchar(ch);
    putchar('\n');
}

// ---------------------------------------------------------------------------
// Run a shell command and display clear feedback about what happened.
// This is the core execution function — it shows the command, runs it,
// then reports whether it succeeded or failed, and explains when
// a command produces no visible output.
// ---------------------------------------------------------------------------
static void execute_and_report(const char *macro_name, const char *command)
{
    printf("\n");
    print_line('-');
    printf("  MACRO   : %s\n", macro_name);
    printf("  COMMAND : %s\n", command);
    print_line('-');
    printf("\n");

    // Flush stdout so the header appears before the command runs
    fflush(stdout);

    int ret = system(command);

    printf("\n");
    print_line('-');

    if (ret == -1) {
        printf("  [ERROR] Could not execute the command.\n");
        printf("  Make sure the command is valid and your shell is working.\n");
    } else {
        int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
        if (exit_code == 0) {
            printf("  [OK] Command completed successfully (exit code 0).\n");
            printf("\n");
            printf("  NOTE: If you see no output above, the command ran but\n");
            printf("  produced no visible results. This is NORMAL for commands\n");
            printf("  like 'find' (no matches), 'rm' (silent success), etc.\n");
        } else if (exit_code > 0) {
            printf("  [DONE] Command finished with exit code %d.\n", exit_code);
            printf("\n");
            printf("  A non-zero exit code usually means something went wrong.\n");
            printf("  Check the output above for error messages.\n");
        } else {
            printf("  [WARN] Command was terminated by a signal.\n");
        }
    }
    print_line('-');
}

// ---------------------------------------------------------------------------
// Keybinding capture via termios
// ---------------------------------------------------------------------------

static void enter_raw_mode(struct termios *saved)
{
    tcgetattr(STDIN_FILENO, saved);
    struct termios raw = *saved;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void leave_raw_mode(const struct termios *saved)
{
    tcsetattr(STDIN_FILENO, TCSANOW, saved);
}

static void parse_csi(const unsigned char *buf, int n, char *result, int result_size)
{
    static const char *mod_table[] = {
        "",               // 0
        "",               // 1
        "shift+",         // 2
        "alt+",           // 3
        "alt+shift+",     // 4
        "ctrl+",          // 5
        "ctrl+shift+",    // 6
        "ctrl+alt+",      // 7
        "ctrl+alt+shift+" // 8
    };

    int i = 2;
    int num1 = 0, num2 = 0;

    if (i < n && buf[i] >= 'A' && buf[i] <= 'Z') {
        switch (buf[i]) {
            case 'A': snprintf(result, result_size, "up");    return;
            case 'B': snprintf(result, result_size, "down");  return;
            case 'C': snprintf(result, result_size, "right"); return;
            case 'D': snprintf(result, result_size, "left");  return;
            case 'H': snprintf(result, result_size, "home");  return;
            case 'F': snprintf(result, result_size, "end");   return;
            default:
                snprintf(result, result_size, "esc[%c", buf[i]);
                return;
        }
    }

    while (i < n && buf[i] >= '0' && buf[i] <= '9')
        num1 = num1 * 10 + (buf[i++] - '0');

    if (i < n && buf[i] == ';') {
        i++;
        while (i < n && buf[i] >= '0' && buf[i] <= '9')
            num2 = num2 * 10 + (buf[i++] - '0');
    }

    const char *mod = (num2 >= 2 && num2 <= 8) ? mod_table[num2] : "";
    char terminator = (i < n) ? (char)buf[i] : '\0';

    if (terminator == '~') {
        switch (num1) {
            case 1:  snprintf(result, result_size, "%shome",    mod); break;
            case 2:  snprintf(result, result_size, "%sinsert",  mod); break;
            case 3:  snprintf(result, result_size, "%sdelete",  mod); break;
            case 4:  snprintf(result, result_size, "%send",     mod); break;
            case 5:  snprintf(result, result_size, "%spageup",  mod); break;
            case 6:  snprintf(result, result_size, "%spagedown",mod); break;
            case 11: snprintf(result, result_size, "%sf1",      mod); break;
            case 12: snprintf(result, result_size, "%sf2",      mod); break;
            case 13: snprintf(result, result_size, "%sf3",      mod); break;
            case 14: snprintf(result, result_size, "%sf4",      mod); break;
            case 15: snprintf(result, result_size, "%sf5",      mod); break;
            case 17: snprintf(result, result_size, "%sf6",      mod); break;
            case 18: snprintf(result, result_size, "%sf7",      mod); break;
            case 19: snprintf(result, result_size, "%sf8",      mod); break;
            case 20: snprintf(result, result_size, "%sf9",      mod); break;
            case 21: snprintf(result, result_size, "%sf10",     mod); break;
            case 23: snprintf(result, result_size, "%sf11",     mod); break;
            case 24: snprintf(result, result_size, "%sf12",     mod); break;
            default: snprintf(result, result_size, "seq:%d~",   num1); break;
        }
    } else if (terminator >= 'A' && terminator <= 'Z') {
        switch (terminator) {
            case 'A': snprintf(result, result_size, "%sup",    mod); break;
            case 'B': snprintf(result, result_size, "%sdown",  mod); break;
            case 'C': snprintf(result, result_size, "%sright", mod); break;
            case 'D': snprintf(result, result_size, "%sleft",  mod); break;
            case 'H': snprintf(result, result_size, "%shome",  mod); break;
            case 'F': snprintf(result, result_size, "%send",   mod); break;
            default:
                snprintf(result, result_size, "%sseq:%c", mod, terminator);
                break;
        }
    } else {
        snprintf(result, result_size, "unknown_seq");
    }
}

static void parse_ss3(const unsigned char *buf, int n, char *result, int result_size)
{
    if (n < 3) {
        snprintf(result, result_size, "escape");
        return;
    }
    switch (buf[2]) {
        case 'P': snprintf(result, result_size, "f1");    break;
        case 'Q': snprintf(result, result_size, "f2");    break;
        case 'R': snprintf(result, result_size, "f3");    break;
        case 'S': snprintf(result, result_size, "f4");    break;
        case 'A': snprintf(result, result_size, "up");    break;
        case 'B': snprintf(result, result_size, "down");  break;
        case 'C': snprintf(result, result_size, "right"); break;
        case 'D': snprintf(result, result_size, "left");  break;
        case 'H': snprintf(result, result_size, "home");  break;
        case 'F': snprintf(result, result_size, "end");   break;
        default:
            snprintf(result, result_size, "ss3+%c", buf[2]);
            break;
    }
}

char *read_keybinding(void)
{
    struct termios saved;
    enter_raw_mode(&saved);

    printf("Press your keybinding (some limitations exist; see README.md for details): ");
    fflush(stdout);

    unsigned char buf[32];
    memset(buf, 0, sizeof(buf));

    int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
        leave_raw_mode(&saved);
        printf("\n");
        return strdup("unknown");
    }

    if (n == 1 && buf[0] == 0x1b) {
        struct termios timeout_termios = saved;
        struct termios current;
        tcgetattr(STDIN_FILENO, &current);
        current.c_cc[VMIN]  = 0;
        current.c_cc[VTIME] = 2;
        tcsetattr(STDIN_FILENO, TCSANOW, &current);

        int extra = (int)read(STDIN_FILENO, buf + 1, sizeof(buf) - 1);
        if (extra > 0) n += extra;
        (void)timeout_termios;
    }

    leave_raw_mode(&saved);
    printf("\n");

    char result[64];
    result[0] = '\0';

    if (n == 1) {
        unsigned char c = buf[0];
        if (c == 0x00) {
            snprintf(result, sizeof(result), "ctrl+@");
        } else if (c >= 0x01 && c <= 0x1a) {
            if (c == 0x0d) {
                snprintf(result, sizeof(result), "enter");
            } else {
                snprintf(result, sizeof(result), "ctrl+%c", 'a' + c - 1);
            }
        } else if (c == 0x1b) {
            snprintf(result, sizeof(result), "escape");
        } else if (c == 0x1c) {
            snprintf(result, sizeof(result), "ctrl+\\");
        } else if (c == 0x1d) {
            snprintf(result, sizeof(result), "ctrl+]");
        } else if (c == 0x1e) {
            snprintf(result, sizeof(result), "ctrl+^");
        } else if (c == 0x1f) {
            snprintf(result, sizeof(result), "ctrl+_");
        } else if (c == 0x7f || c == 0x08) {
            snprintf(result, sizeof(result), "backspace");
        } else if (c == ' ') {
            snprintf(result, sizeof(result), "space");
        } else if (c >= 'A' && c <= 'Z') {
            snprintf(result, sizeof(result), "shift+%c", (char)(c + 32));
        } else if (c > ' ' && c < 0x7f) {
            snprintf(result, sizeof(result), "%c", (char)c);
        } else {
            snprintf(result, sizeof(result), "0x%02x", c);
        }

    } else if (n >= 2 && buf[0] >= 0xc0) {
        int seq_len;
        if      (buf[0] >= 0xf0) seq_len = 4;
        else if (buf[0] >= 0xe0) seq_len = 3;
        else                     seq_len = 2;
        if (seq_len > n) seq_len = n;
        memcpy(result, buf, seq_len);
        result[seq_len] = '\0';

    } else if (n >= 2 && buf[0] == 0x1b && buf[1] == '[') {
        parse_csi(buf, n, result, sizeof(result));

    } else if (n >= 2 && buf[0] == 0x1b && buf[1] == 'O') {
        parse_ss3(buf, n, result, sizeof(result));

    } else if (n >= 2 && buf[0] == 0x1b) {
        unsigned char c = buf[1];
        if (c == 0x1b) {
            snprintf(result, sizeof(result), "alt+escape");
        } else if (c >= 0x01 && c <= 0x1a) {
            snprintf(result, sizeof(result), "ctrl+alt+%c", 'a' + c - 1);
        } else if (c >= 'a' && c <= 'z') {
            snprintf(result, sizeof(result), "alt+%c", (char)c);
        } else if (c >= 'A' && c <= 'Z') {
            snprintf(result, sizeof(result), "alt+shift+%c", (char)(c + 32));
        } else if (c == ' ') {
            snprintf(result, sizeof(result), "alt+space");
        } else if (c >= 0xc0) {
            int seq_len;
            if      (c >= 0xf0) seq_len = 4;
            else if (c >= 0xe0) seq_len = 3;
            else                seq_len = 2;
            if (seq_len > n - 1) seq_len = n - 1;
            char utf8char[8] = {0};
            memcpy(utf8char, buf + 1, seq_len);
            snprintf(result, sizeof(result), "alt+%s", utf8char);
        } else {
            snprintf(result, sizeof(result), "alt+0x%02x", c);
        }

    } else {
        char hex[64] = "";
        for (int i = 0; i < n && i < 10; i++) {
            char h[4];
            snprintf(h, sizeof(h), "%02x", buf[i]);
            strncat(hex, h, sizeof(hex) - strlen(hex) - 1);
        }
        snprintf(result, sizeof(result), "seq:%s", hex);
    }

    return strdup(result);
}

// ---------------------------------------------------------------------------
// CSV parsing — handles commands that contain commas
// ---------------------------------------------------------------------------

static int parse_csv_line(char *line,
                          char **out_name,
                          char **out_command,
                          char **out_keybinding)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    char *start = line;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0' || *start == '#') return 0;

    char *first_comma = strchr(start, ',');
    if (!first_comma) return 0;

    char *last_comma = strrchr(start, ',');

    *first_comma = '\0';
    *out_name = trim(start);

    if (first_comma == last_comma) {
        *out_command    = trim(first_comma + 1);
        *out_keybinding = "";
    } else {
        *last_comma = '\0';
        *out_command    = trim(first_comma + 1);
        *out_keybinding = trim(last_comma + 1);
    }

    if (strcmp(*out_keybinding, "none") == 0 ||
        strcmp(*out_keybinding, "None") == 0 ||
        strcmp(*out_keybinding, "NONE") == 0)
    {
        *out_keybinding = "";
    }

    if ((*out_name)[0] == '\0' || (*out_command)[0] == '\0')
        return 0;

    return 1;
}

// ---------------------------------------------------------------------------
// Macro storage helpers
// ---------------------------------------------------------------------------

void write_macro(struct macro macro, FILE *file)
{
    const char *kb = (macro.keybinding && macro.keybinding[0] != '\0')
                     ? macro.keybinding : "none";
    fprintf(file, "%s,%s,%s\n", macro.name, macro.command, kb);
}

struct macro *macros = NULL;
int num_macros = 0;

bool read_macros(const char *config_path)
{
    FILE *file = fopen(config_path, "r");
    if (!file) return false;

    for (int i = 0; i < num_macros; i++) {
        free(macros[i].name);
        free(macros[i].command);
        free(macros[i].keybinding);
    }
    free(macros);
    macros = NULL;
    num_macros = 0;

    int capacity = 16;
    macros = malloc(capacity * sizeof(struct macro));
    if (!macros) { perror("malloc"); fclose(file); exit(EXIT_FAILURE); }

    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        char *name, *command, *keybinding;
        if (!parse_csv_line(line, &name, &command, &keybinding))
            continue;

        if (num_macros >= capacity) {
            capacity *= 2;
            macros = realloc(macros, capacity * sizeof(struct macro));
            if (!macros) { perror("realloc"); fclose(file); exit(EXIT_FAILURE); }
        }

        macros[num_macros].name       = strdup(name);
        macros[num_macros].command    = strdup(command);
        macros[num_macros].keybinding = strdup(keybinding);
        num_macros++;
    }

    fclose(file);

    if (num_macros > 0)
        macros = realloc(macros, num_macros * sizeof(struct macro));
    else { free(macros); macros = NULL; }

    return num_macros > 0;
}

void create_new_macro(char *name, char *command, char *keybinding, const char *config_path)
{
    struct macro macro;
    macro.name       = strdup(name);
    macro.command    = strdup(command);
    macro.keybinding = strdup(keybinding);

    num_macros++;
    macros = realloc(macros, num_macros * sizeof(struct macro));
    if (!macros) { perror("realloc"); exit(EXIT_FAILURE); }
    macros[num_macros - 1] = macro;

    FILE *file = fopen(config_path, "a");
    if (!file) { perror("fopen"); exit(EXIT_FAILURE); }
    write_macro(macro, file);
    fclose(file);
}

void delete_macro(const char *name, const char *config_path)
{
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            free(macros[i].name);
            free(macros[i].command);
            free(macros[i].keybinding);
            for (int j = i; j < num_macros - 1; j++)
                macros[j] = macros[j + 1];
            num_macros--;
            if (num_macros > 0)
                macros = realloc(macros, num_macros * sizeof(struct macro));
            else { free(macros); macros = NULL; }

            FILE *file = fopen(config_path, "w");
            if (!file) { perror("fopen"); exit(EXIT_FAILURE); }
            for (int j = 0; j < num_macros; j++)
                write_macro(macros[j], file);
            fclose(file);
            printf("Macro \"%s\" deleted.\n", name);
            return;
        }
    }
    printf("No macro found with name \"%s\".\n", name);
}

void run_macro_by_name(const char *name)
{
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            execute_and_report(macros[i].name, macros[i].command);
            return;
        }
    }
    printf("No macro found with name \"%s\".\n", name);
    printf("\nAvailable macros:\n");
    for (int i = 0; i < num_macros; i++)
        printf("  - %s\n", macros[i].name);
}

void run_macro_by_keybinding(const char *keybinding)
{
    for (int i = 0; i < num_macros; i++) {
        if (macros[i].keybinding[0] == '\0') continue;
        if (strcmp(macros[i].keybinding, keybinding) == 0) {
            execute_and_report(macros[i].name, macros[i].command);
            return;
        }
    }
    printf("No macro found for keybinding \"%s\".\n", keybinding);
    printf("\nMacros with keybindings:\n");
    for (int i = 0; i < num_macros; i++) {
        if (macros[i].keybinding[0] != '\0')
            printf("  - %-16s  [%s]\n", macros[i].name, macros[i].keybinding);
    }
}

static void save_all_macros(const char *config_path)
{
    FILE *file = fopen(config_path, "w");
    if (!file) { perror("fopen"); return; }
    for (int i = 0; i < num_macros; i++)
        write_macro(macros[i], file);
    fclose(file);
}

int import_from_csv(const char *import_path, const char *config_path)
{
    FILE *file = fopen(import_path, "r");
    if (!file) {
        printf("  [ERROR] Could not open file: %s\n", import_path);
        perror("  fopen");
        return 0;
    }

    char line[2048];
    int imported = 0, updated = 0;

    while (fgets(line, sizeof(line), file)) {
        char *name, *command, *keybinding;
        if (!parse_csv_line(line, &name, &command, &keybinding))
            continue;

        bool found = false;
        for (int i = 0; i < num_macros; i++) {
            if (strcmp(macros[i].name, name) == 0) {
                free(macros[i].command);
                free(macros[i].keybinding);
                macros[i].command    = strdup(command);
                macros[i].keybinding = strdup(keybinding);
                found = true;
                updated++;
                break;
            }
        }

        if (!found) {
            num_macros++;
            macros = realloc(macros, num_macros * sizeof(struct macro));
            if (!macros) { perror("realloc"); fclose(file); exit(EXIT_FAILURE); }
            macros[num_macros - 1].name       = strdup(name);
            macros[num_macros - 1].command    = strdup(command);
            macros[num_macros - 1].keybinding = strdup(keybinding);
        }

        imported++;
    }

    fclose(file);

    if (imported > 0) {
        save_all_macros(config_path);
        printf("\n  Import summary:\n");
        printf("    New macros added : %d\n", imported - updated);
        printf("    Macros updated   : %d\n", updated);
        printf("    Total in library : %d\n", num_macros);

        // Show what was imported with keybindings
        printf("\n  Imported macros:\n");
        print_line('-');
        printf("  %-20s  %-30s  %s\n", "NAME", "COMMAND", "KEYBINDING");
        print_line('-');
        // Reload and show last N imported
        for (int i = 0; i < num_macros; i++) {
            const char *kb = (macros[i].keybinding[0] != '\0')
                             ? macros[i].keybinding : "(none)";
            printf("  %-20s  %-30s  %s\n",
                   macros[i].name, macros[i].command, kb);
        }
        print_line('-');
    }

    return imported;
}

// ---------------------------------------------------------------------------
// Help / Information panel
// ---------------------------------------------------------------------------
static void show_help(void)
{
    system("clear");
    print_line('=');
    printf("\n");
    printf("  MacroForge — Help & Quick Start Guide\n");
    printf("  Work Smarter, Not Harder\n");
    printf("\n");
    print_line('=');

    printf("\n");
    printf("  WHAT IS MACROFORGE?\n");
    printf("  -------------------\n");
    printf("  MacroForge saves terminal commands as \"macros\" so you can run\n");
    printf("  them instantly — by typing a name or pressing a keybinding.\n");
    printf("\n");
    printf("  Example: Instead of typing 'git add . && git commit -m \"update\"'\n");
    printf("  every time, save it as a macro called \"Quick Commit\" and bind\n");
    printf("  it to Ctrl+G. Now just press Ctrl+G and it runs automatically.\n");
    printf("\n");
    print_line('-');

    printf("\n");
    printf("  GETTING STARTED (3 easy steps):\n");
    printf("  ================================\n");
    printf("\n");
    printf("  Step 1: ADD MACROS\n");
    printf("    Option [1]  — Type your own command (name + command + keybinding)\n");
    printf("    Option [2]  — Pick from preset commands (delete, copy, move, etc.)\n");
    printf("    Option [10] — Import from a CSV file (e.g. from the web app)\n");
    printf("\n");
    printf("  Step 2: RUN MACROS\n");
    printf("    Option [3]  — Type the macro's NAME and it runs\n");
    printf("    Option [4]  — Press the KEYBINDING (e.g. Ctrl+L) and it runs\n");
    printf("\n");
    printf("  Step 3: MANAGE MACROS\n");
    printf("    Option [5]  — List all macros (see names, commands, keybindings)\n");
    printf("    Option [6]  — Delete a macro by name\n");
    printf("    Option [7]  — Clear ALL macros\n");
    printf("    Option [8]  — Edit a macro's name, command, or keybinding\n");
    printf("    Option [9]  — Edit a macro by pressing its keybinding\n");
    printf("\n");
    print_line('-');

    printf("\n  Press Enter to see more...");
    getchar();

    system("clear");
    print_line('=');
    printf("\n");
    printf("  IMPORTING FROM THE WEB APP (MacroForge Web)\n");
    printf("  ============================================\n");
    printf("\n");
    printf("  The MacroForge web app lets you create and organize macros\n");
    printf("  with a visual interface and AI assistance. Here's how to\n");
    printf("  transfer them to this terminal program:\n");
    printf("\n");
    printf("  1. In the web app, click the \"Export\" tab at the top\n");
    printf("  2. Click \"Select All\" (or check individual macros)\n");
    printf("  3. Click the \"CSV\" button to switch to CSV format\n");
    printf("  4. Click the green \"Download\" button\n");
    printf("  5. Open your terminal (NOT VS Code terminal) and type:\n");
    printf("\n");
    printf("     make && ./macro ~/Downloads/macroforge_export.csv\n");
    printf("\n");
    printf("  That's it! All macros AND their keybindings are imported.\n");
    printf("\n");
    printf("  ALTERNATIVE: If you're already in the program, use [10]\n");
    printf("  and type or drag-drop the file path (no quotes needed).\n");
    printf("\n");
    printf("  The CSV format is: name,command,keybinding\n");
    printf("  Example line:  Git Status,git status,ctrl+g\n");
    printf("\n");
    print_line('-');

    printf("\n");
    printf("  KEYBINDING TIPS\n");
    printf("  ===============\n");
    printf("\n");
    printf("  Keybindings that WORK:\n");
    printf("    Ctrl+A through Ctrl+Z     (e.g. ctrl+g, ctrl+l)\n");
    printf("    Alt+key                   (e.g. alt+g, alt+shift+f)\n");
    printf("    Function keys             (f1 through f12)\n");
    printf("    Arrow keys + modifiers    (ctrl+up, alt+left, etc.)\n");
    printf("\n");
    printf("  Keybindings to AVOID:\n");
    printf("    Ctrl+C  — sends interrupt signal (kills the program!)\n");
    printf("    Ctrl+Z  — suspends the program\n");
    printf("    Ctrl+D  — sends EOF\n");
    printf("    Ctrl+S  — freezes terminal output\n");
    printf("    Ctrl+Q  — unfreezes terminal output\n");
    printf("\n");
    printf("  Note: Ctrl+Shift+key is indistinguishable from Ctrl+key\n");
    printf("  in most terminals. This is a terminal limitation, not a bug.\n");
    printf("\n");
    print_line('-');

    printf("\n  Press Enter to see more...");
    getchar();

    system("clear");
    print_line('=');
    printf("\n");
    printf("  TROUBLESHOOTING\n");
    printf("  ===============\n");
    printf("\n");
    printf("  Q: I ran a macro but nothing happened!\n");
    printf("  A: The command DID run. Some commands produce no visible\n");
    printf("     output when they succeed. For example:\n");
    printf("       • 'find . -size +50M' shows nothing if no big files exist\n");
    printf("       • 'rm file.txt' shows nothing on success\n");
    printf("       • 'cp a.txt b.txt' shows nothing on success\n");
    printf("     Check the [OK] / [DONE] exit code shown after running.\n");
    printf("     Exit code 0 = success. Non-zero = error.\n");
    printf("\n");
    printf("  Q: My keybinding from the web app doesn't work!\n");
    printf("  A: Make sure the keybinding format matches. The web app\n");
    printf("     uses text like \"ctrl+l\" which maps to pressing Ctrl+L.\n");
    printf("     Check Option [5] to see what keybindings are loaded.\n");
    printf("\n");
    printf("  Q: I exported CSV but the import found 0 macros!\n");
    printf("  A: Make sure you chose CSV format in the web app's Export tab.\n");
    printf("     The file should have lines like: name,command,keybinding\n");
    printf("     Lines starting with # are treated as comments and skipped.\n");
    printf("\n");
    printf("  Q: VS Code intercepts my keybinding!\n");
    printf("  A: Some keybindings (like Ctrl+L) are used by VS Code.\n");
    printf("     To fix this, run MacroForge in an EXTERNAL terminal\n");
    printf("     (Terminal.app on Mac, etc.) instead\n");
    printf("     of VS Code's built-in terminal. Or use keybindings that\n");
    printf("     VS Code doesn't capture (Alt+key, F-keys, etc.).\n");
    printf("\n");
    print_line('-');

    printf("\n");
    printf("  FILE LOCATIONS\n");
    printf("  ==============\n");
    printf("  Macros saved at: ~/.config/hackathon/macros.csv\n");
    printf("  You can edit this file directly with any text editor.\n");
    printf("\n");
    print_line('=');
}

// ---------------------------------------------------------------------------
// Main program loop
// ---------------------------------------------------------------------------
void main_loop(const char *config_path)
{
    char macro_name[256], macro_command[1024];

    system("clear");
    print_line('=');
    switch (mode) {
        // ------------------------------------------------------------------
        case MODE_NORMAL: {
            printf("\n");
            printf("  MacroForge — Work Smarter, Not Harder\n");
            printf("  %d macro%s loaded\n", num_macros, num_macros == 1 ? "" : "s");
            printf("\n");
            print_line('-');
            printf("  CREATE                           MANAGE\n");
            printf("  [1]  Create custom macro         [5]  List all macros\n");
            printf("  [2]  Create from preset          [6]  Delete a macro\n");
            printf("                                   [7]  Clear all macros\n");
            printf("  RUN                              [8]  Edit macro by name\n");
            printf("  [3]  Run macro by name           [9]  Edit macro by keybinding\n");
            printf("  [4]  Run macro by keybinding\n");
            printf("                                   OTHER\n");
            printf("  IMPORT                           [11] Help / Quick Guide\n");
            printf("  [10] Import from CSV file        [12] Quit\n");
            print_line('-');
            int choice;
            printf("\n Choose an option > ");

            if (scanf("%d", &choice) == EOF) {
                system("clear");
                exit(EXIT_SUCCESS);
            }
            clear_stdin();
            switch (choice) {
                case 1:  mode = MODE_INSERT_CUSTOM;         break;
                case 2:  mode = MODE_INSERT_PRESET;         break;
                case 3:  mode = MODE_RUN_BY_NAME;           break;
                case 4:  mode = MODE_RUN_BY_KEYBINDING;     break;
                case 5:  mode = MODE_LIST_MACROS;           break;
                case 6:  mode = MODE_DELETE_MACRO;           break;
                case 7:  mode = MODE_CLEAR_MACROS;           break;
                case 8:  mode = MODE_EDIT_MACRO_BY_NAME;     break;
                case 9:  mode = MODE_EDIT_MACRO_BY_KEYBIND;  break;
                case 10: mode = MODE_IMPORT_CSV;             break;
                case 11: mode = MODE_HELP;                   break;
                case 12: system("clear"); exit(EXIT_SUCCESS);
                default: break;
            }
            break;
        }

        // ------------------------------------------------------------------
        case MODE_INSERT_CUSTOM: {
            printf("\n  CREATE A NEW MACRO\n");
            print_line('-');
            printf("\n");
            printf("  Step 1/3 — Name your macro (e.g. \"Git Status\"):\n  > ");
            scanf("%255[^\n]", macro_name);
            clear_stdin();

            printf("\n  Step 2/3 — Press the keybinding you want to assign:\n");
            char *keybinding = read_keybinding();
            printf("  Captured keybinding: %s\n", keybinding);

            printf("\n  Step 3/3 — Enter the terminal command to run:\n");
            printf("  (e.g. git status, ls -la, make && ./deploy.sh)\n  > ");
            scanf(" %1023[^\n]", macro_command);
            clear_stdin();

            create_new_macro(macro_name, macro_command, keybinding, config_path);
            free(keybinding);

            printf("\nMacro \"%s\" created successfully!\n", macro_name);
            print_line('-');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_INSERT_PRESET: {
            printf("Enter macro name: ");
            scanf("%255[^\n]", macro_name);
            clear_stdin();

            char *keybinding = read_keybinding();
            printf("Captured keybinding: %s\n", keybinding);

            printf("What do you want the macro to do?\n");
            printf("[1] Remove file(s) or folder(s)\n");
            printf("[2] Rename file\n");
            printf("[3] Move file(s)\n");
            printf("[4] Copy file(s)\n");
            printf("[5] Open file(s) or URL\n");
            printf("[6] Display date and time\n");
            printf("[7] Open manual page\n");
            printf("[8] Display all files in current directory with details\n");
            printf("[9] Execute executable or shell script\n");

            printf("> ");
            int choice;
            scanf("%d", &choice);
            clear_stdin();

            char src[512] = {0}, dst[512] = {0};

            switch (choice) {
                case 1:
                    printf("File path(s) to remove (spaces separate paths):\n");
                    scanf(" %511[^\n]", src);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "rm -r %s", src);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 2:
                    printf("File path to rename: ");
                    scanf("%511s", src);
                    clear_stdin();
                    printf("New file path: ");
                    scanf("%511s", dst);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "mv %s %s", src, dst);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 3:
                    printf("File path(s) to move: ");
                    scanf("%511s", src);
                    clear_stdin();
                    printf("Destination directory: ");
                    scanf("%511s", dst);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "mv %s %s", src, dst);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 4:
                    printf("File path(s) to copy: ");
                    scanf("%511s", src);
                    clear_stdin();
                    printf("Destination path or directory: ");
                    scanf("%511s", dst);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "cp %s %s", src, dst);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 5:
                    printf("File path(s) or URL(s) to open (spaces separate entries):\n");
                    scanf(" %511[^\n]", src);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "open %s", src);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 6:
                    create_new_macro(macro_name, "date", keybinding, config_path);
                    break;
                case 7:
                    printf("Manual page name: ");
                    scanf("%511s", src);
                    clear_stdin();
                    snprintf(macro_command, sizeof(macro_command), "man %s", src);
                    create_new_macro(macro_name, macro_command, keybinding, config_path);
                    break;
                case 8:
                    create_new_macro(macro_name, "ls -la", keybinding, config_path);
                    break;
                case 9:
                    printf("Enter name of executable or shell script: ");
                    scanf("%511s", src);
                    clear_stdin();
                    create_new_macro(macro_name, src, keybinding, config_path);
                    break;
                default:
                    printf("Invalid choice.\n");
                    break;
            }

            free(keybinding);
            printf("\nMacro \"%s\" created successfully!\n", macro_name);
            print_line('-');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_RUN_BY_NAME: {
            printf("\nEnter macro name: ");
            scanf("%255[^\n]", macro_name);
            clear_stdin();
            run_macro_by_name(macro_name);

            printf("\nPress Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_RUN_BY_KEYBINDING: {
            char *keybinding = read_keybinding();
            printf("Looking up keybinding: %s\n", keybinding);
            run_macro_by_keybinding(keybinding);
            free(keybinding);

            printf("\nPress Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        case MODE_LIST_MACROS: {
            printf("\n");
            printf("  %-4s  %-20s  %-30s  %s\n", "#", "NAME", "COMMAND", "KEYBINDING");
            print_line('-');

            for (int i = 0; i < num_macros; i++) {
                const char *kb = (macros[i].keybinding[0] != '\0')
                                 ? macros[i].keybinding : "(none)";
                printf("  %-4d  %-20s  %-30s  %s\n",
                       i + 1, macros[i].name, macros[i].command, kb);
            }

            if (num_macros == 0) {
                printf("  (no macros loaded)\n");
                printf("\n  TIP: Use [1] to create a macro, or [10] to import from CSV.\n");
            }

            print_line('-');
            printf("  Total: %d macro%s\n", num_macros, num_macros == 1 ? "" : "s");

            printf("\nPress Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        case MODE_DELETE_MACRO: {
            printf("Enter macro name: ");
            scanf("%255[^\n]", macro_name);
            clear_stdin();
            delete_macro(macro_name, config_path);

            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        case MODE_CLEAR_MACROS: {
            for (int i = 0; i < num_macros; i++) {
                free(macros[i].name);
                free(macros[i].command);
                free(macros[i].keybinding);
            }
            free(macros);
            macros = NULL;
            num_macros = 0;

            FILE *clear_file = fopen(config_path, "w");
            if (!clear_file) { perror("fopen"); exit(EXIT_FAILURE); }
            fclose(clear_file);

            printf("All macros cleared.\n");
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_EDIT_MACRO_BY_NAME: {
            printf("Enter macro name to edit: ");
            char edit_name[256] = {0};
            scanf("%255[^\n]", edit_name);
            clear_stdin();

            int found = -1;
            for (int i = 0; i < num_macros; i++) {
                if (strcmp(macros[i].name, edit_name) == 0) {
                    found = i;
                    break;
                }
            }

            if (found == -1) {
                printf("No macro found with name \"%s\".\n", edit_name);
                printf("\nAvailable macros:\n");
                for (int i = 0; i < num_macros; i++)
                    printf("  - %s\n", macros[i].name);
            } else {
                int idx = found;
                const char *kb = (macros[idx].keybinding[0] != '\0')
                                 ? macros[idx].keybinding : "(none)";
                printf("\nCurrent name:       %s\n", macros[idx].name);
                printf("Current command:    %s\n", macros[idx].command);
                printf("Current keybinding: %s\n\n", kb);

                char new_name[256] = {0};
                printf("New name (press Enter to keep \"%s\"): ", macros[idx].name);
                scanf("%255[^\n]", new_name);
                clear_stdin();
                if (new_name[0] != '\0') {
                    free(macros[idx].name);
                    macros[idx].name = strdup(new_name);
                }

                char new_command[1024] = {0};
                printf("New command (press Enter to keep current): ");
                scanf("%1023[^\n]", new_command);
                clear_stdin();
                if (new_command[0] != '\0') {
                    free(macros[idx].command);
                    macros[idx].command = strdup(new_command);
                }

                printf("Press new keybinding (press Enter/Return to keep current): ");
                char *new_keybinding = read_keybinding();
                if (strcmp(new_keybinding, "enter") != 0) {
                    free(macros[idx].keybinding);
                    macros[idx].keybinding = strdup(new_keybinding);
                }
                free(new_keybinding);

                save_all_macros(config_path);
                printf("Macro updated.\n");
            }

            print_line('-');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_EDIT_MACRO_BY_KEYBIND: {
            printf("Press the keybinding of the macro to edit: ");
            char *search_kb = read_keybinding();
            printf("Looking up: %s\n", search_kb);

            int found = -1;
            for (int i = 0; i < num_macros; i++) {
                if (macros[i].keybinding[0] != '\0' &&
                    strcmp(macros[i].keybinding, search_kb) == 0) {
                    found = i;
                    break;
                }
            }
            free(search_kb);

            if (found == -1) {
                printf("No macro found for that keybinding.\n");
                printf("\nMacros with keybindings:\n");
                for (int i = 0; i < num_macros; i++) {
                    if (macros[i].keybinding[0] != '\0')
                        printf("  - %-16s  [%s]\n", macros[i].name, macros[i].keybinding);
                }
            } else {
                int idx = found;
                printf("\nCurrent name:       %s\n", macros[idx].name);
                printf("Current command:    %s\n", macros[idx].command);
                printf("Current keybinding: %s\n\n", macros[idx].keybinding);

                char new_name[256] = {0};
                printf("New name (press Enter to keep \"%s\"): ", macros[idx].name);
                scanf("%255[^\n]", new_name);
                clear_stdin();
                if (new_name[0] != '\0') {
                    free(macros[idx].name);
                    macros[idx].name = strdup(new_name);
                }

                char new_command[1024] = {0};
                printf("New command (press Enter to keep current): ");
                scanf("%1023[^\n]", new_command);
                clear_stdin();
                if (new_command[0] != '\0') {
                    free(macros[idx].command);
                    macros[idx].command = strdup(new_command);
                }

                printf("Press new keybinding (press Enter/Return to keep current):\n");
                char *new_keybinding = read_keybinding();
                if (strcmp(new_keybinding, "enter") != 0) {
                    free(macros[idx].keybinding);
                    macros[idx].keybinding = strdup(new_keybinding);
                }
                free(new_keybinding);

                save_all_macros(config_path);
                printf("Macro updated.\n");
            }

            print_line('-');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_IMPORT_CSV: {
            printf("\n");
            printf("  IMPORT MACROS FROM CSV FILE\n");
            print_line('-');
            printf("\n");
            printf("  This imports macros from a CSV file (e.g. from the web app).\n");
            printf("  Expected format: name,command,keybinding\n");
            printf("  Example line:    Git Status,git status,ctrl+g\n\n");
            printf("  HOW TO GET THE FILE:\n");
            printf("  1. Open MacroForge web app\n");
            printf("  2. Go to Export tab → Select macros → Choose CSV → Download\n");
            printf("  3. The file is saved to your Downloads folder\n\n");
            printf("  TIP: Drag the file into this terminal to auto-fill the path.\n");
            printf("       Quotes and backslashes are handled automatically.\n\n");

            char import_path[512] = {0};
            printf("  Enter path to CSV file: ");
            fflush(stdout);

            if (fgets(import_path, sizeof(import_path), stdin) == NULL) {
                import_path[0] = '\0';
            }

            // Clean up: trim, strip quotes, unescape backslashes
            clean_path(import_path);

            if (import_path[0] == '\0') {
                printf("  No path entered.\n");
            } else {
                printf("\n  Opening: %s\n\n", import_path);
                int count = import_from_csv(import_path, config_path);
                if (count > 0) {
                    printf("\n  Successfully imported %d macro%s!\n",
                           count, count == 1 ? "" : "s");
                } else {
                    printf("  No macros found in the file.\n");
                    printf("  Make sure the file exists and has the correct format:\n");
                    printf("    name,command,keybinding\n\n");
                    printf("  Common fixes:\n");
                    printf("    • Make sure you recompiled: make\n");
                    printf("    • Try dragging the file here instead of typing the path\n");
                    printf("    • Check that the file was exported as CSV (not Shell or JSON)\n");
                }
            }

            print_line('-');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_HELP: {
            show_help();
            printf("\nPress Enter to return to menu...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    // Give help too many arguments are provided
    if (argc > 2) {
        printf("Usage: %s [path/to/exported.csv]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *home_dir    = getenv("HOME");
    char *config_path = malloc(strlen(home_dir) + strlen("/.config/hackathon/macros.csv") + 1);
    sprintf(config_path, "%s/.config/hackathon/macros.csv", home_dir);

    char *config_dir = malloc(strlen(home_dir) + strlen("/.config") + 1);
    sprintf(config_dir, "%s/.config", home_dir);
    if (mkdir(config_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir: could not create ~/.config");
        free(config_dir); free(config_path);
        exit(EXIT_FAILURE);
    }
    free(config_dir);

    char *hackathon_dir = malloc(strlen(home_dir) + strlen("/.config/hackathon") + 1);
    sprintf(hackathon_dir, "%s/.config/hackathon", home_dir);
    if (mkdir(hackathon_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir: could not create ~/.config/hackathon");
        free(hackathon_dir); free(config_path);
        exit(EXIT_FAILURE);
    }
    free(hackathon_dir);

    FILE *file = fopen(config_path, "a");
    if (!file) {
        perror("fopen: could not open macros file");
        free(config_path);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    struct winsize window_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);
    screen_width = window_size.ws_col;
    if (screen_width == 0) screen_width = 80;

    bool has_macros = read_macros(config_path);

    // -----------------------------------------------------------------------
    // If a CSV file was passed as argument, import it automatically.
    // Usage:  ./macro path/to/exported.csv
    // -----------------------------------------------------------------------
    if (argc == 2) {
        // Copy argv[1] so we can clean up the path
        char import_arg[512];
        strncpy(import_arg, argv[1], sizeof(import_arg) - 1);
        import_arg[sizeof(import_arg) - 1] = '\0';
        clean_path(import_arg);

        printf("\n");
        print_line('=');
        printf("  MacroForge — Importing CSV\n");
        print_line('=');
        printf("\n  File: %s\n\n", import_arg);

        int count = import_from_csv(import_arg, config_path);
        if (count > 0) {
            printf("\n  [OK] Import complete! %d macro%s ready to use.\n",
                   count, count == 1 ? "" : "s");
            printf("\n  Quick start:\n");
            printf("    • Choose [3] to run a macro by name\n");
            printf("    • Choose [4] to run a macro by keybinding\n");
            printf("    • Choose [5] to see all loaded macros\n");
            has_macros = true;
        } else {
            printf("  [ERROR] No macros found in the file.\n");
            printf("  Expected format: name,command,keybinding\n");
        }
        print_line('=');
        printf("\nPress Enter to continue...");
        getchar();
    }

    if (!has_macros && argc < 2) {
        system("clear");
        print_line('=');
        printf("\n");
        printf("  Welcome to MacroForge!\n");
        printf("  Work Smarter, Not Harder\n");
        printf("\n");
        print_line('-');
        printf("\n");
        printf("  You have no macros yet. Here's how to get started:\n\n");
        printf("  OPTION A — Import from the MacroForge web app:\n");
        printf("    1. In the web app, go to Export tab\n");
        printf("    2. Select your macros, choose CSV, click Download\n");
        printf("    3. Quit this program (Ctrl+C) and run:\n");
        printf("       ./macro ~/Downloads/macroforge_export.csv\n\n");
        printf("  OPTION B — Create your first macro right now:\n");
        printf("    Just follow the prompts below.\n");
        printf("\n");
        print_line('=');
        printf("\n  Press Enter to create your first macro...");
        getchar();
        mode = MODE_INSERT_CUSTOM;
    }

    while (true)
        main_loop(config_path);

    free(config_path);
    return 0;
}
