#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <fcntl.h>

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
// Keybinding capture via termios
// ---------------------------------------------------------------------------

// Enters "raw" mode, which disables line buffering and echoing, allowing
// keybindings to be captured directly from the terminal.
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

// Leaves "raw" mode, restoring the terminal to its previous state.
static void leave_raw_mode(const struct termios *saved)
{
    tcsetattr(STDIN_FILENO, TCSANOW, saved);
}

// Parse a CSI (Control Sequence Introducer) sequence (ESC [ ...) that has already been read into buf[0..n-1].
// Converts from the CSI standard and writes a human-readable string into result (size result_size).
static void parse_csi(const unsigned char *buf, int n, char *result, int result_size)
{
    // Modifier map: ESC[1;Nm  where N encodes Shift/Alt/Ctrl combos
    // N: 2=Shift 3=Alt 4=Alt+Shift 5=Ctrl 6=Ctrl+Shift 7=Ctrl+Alt 8=Ctrl+Alt+Shift
    // Note that some of these combinations are not supported by all terminals, as stated in README.md.
    static const char *mod_table[] = {
        "",               // 0 – unused
        "",               // 1 – no modifier
        "shift+",         // 2
        "alt+",           // 3
        "alt+shift+",     // 4
        "ctrl+",          // 5
        "ctrl+shift+",    // 6
        "ctrl+alt+",      // 7
        "ctrl+alt+shift+" // 8
    };

    int i = 2; // skip ESC [
    int num1 = 0, num2 = 0;

    // Direct letter after ESC[ with no number
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

    // Collect first number
    while (i < n && buf[i] >= '0' && buf[i] <= '9')
        num1 = num1 * 10 + (buf[i++] - '0');

    // Optional ;N modifier
    if (i < n && buf[i] == ';') {
        i++;
        while (i < n && buf[i] >= '0' && buf[i] <= '9')
            num2 = num2 * 10 + (buf[i++] - '0');
    }

    const char *mod = (num2 >= 2 && num2 <= 8) ? mod_table[num2] : "";
    char terminator = (i < n) ? (char)buf[i] : '\0';

    // Collect terminator character
    // Converts '~' CSI terminators to human-readable key names, e.g. '3~' -> "delete"
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
    // Converts arrow/navigation keys with modifiers to human-readable key names, e.g. '1;5A' -> "ctrl+up"
    } else if (terminator >= 'A' && terminator <= 'Z') {
        // Arrow/navigation keys with modifiers: ESC[1;5A = ctrl+up
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
    // Otherwise, treat as an unknown sequence
    } else {
        snprintf(result, result_size, "unknown_seq");
    }
}

// Parse an SS3 (Single Shift 3, ) sequence (ESC O ...).
// SS3 is like CSI but with different key mappings.
// Both functions exist for portability, as some terminal emulators use these sequences instead of CSI.
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

// Prompt the user to press a key combination and return a newly-allocated
// string describing it (e.g. "ctrl+a", "alt+shift+f", "f5", "ctrl+up").
// The caller is responsible for freeing the returned string.
char *read_keybinding(void)
{
    struct termios saved;
    enter_raw_mode(&saved);

    printf("Press your keybinding (some limitations exist; see README.md for details): ");
    fflush(stdout);

    unsigned char buf[32];
    memset(buf, 0, sizeof(buf));

    // Read up to sizeof(buf) bytes at once.  VMIN=1 guarantees at least 1 byte
    // returns, but reading more lets multi-byte sequences (UTF-8 Option+key,
    // escape sequences) arrive in a single call instead of being split.
    int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
        leave_raw_mode(&saved);
        printf("\n");
        return strdup("unknown");
    }

    // If the first byte is ESC, try to read the rest of the escape sequence
    // with a short timeout so we don't block forever.
    // Only do a timeout follow-up if we got a bare ESC and nothing else —
    // the rest of the escape sequence may not have arrived yet.
    if (n == 1 && buf[0] == 0x1b) {
        struct termios timeout_termios = saved;
        // Get current raw settings first, then adjust timeout
        struct termios current;
        tcgetattr(STDIN_FILENO, &current);
        current.c_cc[VMIN]  = 0;
        current.c_cc[VTIME] = 2; // 0.2 seconds
        tcsetattr(STDIN_FILENO, TCSANOW, &current);

        int extra = (int)read(STDIN_FILENO, buf + 1, sizeof(buf) - 1);
        if (extra > 0) n += extra;
        (void)timeout_termios; // suppress unused warning
    }

    leave_raw_mode(&saved);
    printf("\n");

    // -----------------------------------------------------------------------
    // Interpret the byte sequence
    // -----------------------------------------------------------------------
    char result[64];
    result[0] = '\0';

    if (n == 1) {
        unsigned char c = buf[0];
        // Control characters must be checked FIRST (before \n, \t, etc.)
        // because bytes like 0x0A (Ctrl+J) and 0x09 (Ctrl+I) would otherwise
        // be mis-labelled as "enter" and "tab".
        if (c == 0x00) {
            // Ctrl+@ / Ctrl+Space
            snprintf(result, sizeof(result), "ctrl+@");
        } else if (c >= 0x01 && c <= 0x1a) {
            // Ctrl+A (0x01) .. Ctrl+Z (0x1a).
            // 0x0D (CR) is what Enter sends in raw mode with ICRNL off;
            // label it "enter" so it round-trips sensibly.
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
        // UTF-8 multi-byte sequence — macOS Option+key sends the Unicode
        // character directly (e.g. Option+P → π, 0xCF 0x80).
        // The terminal gives us no modifier information; store the raw UTF-8
        // bytes so they round-trip correctly as the keybinding string.
        int seq_len;
        if      (buf[0] >= 0xf0) seq_len = 4;
        else if (buf[0] >= 0xe0) seq_len = 3;
        else                     seq_len = 2;  // 0xC0-0xDF
        if (seq_len > n) seq_len = n;           // clamp to bytes we actually got
        memcpy(result, buf, seq_len);
        result[seq_len] = '\0';

    } else if (n >= 2 && buf[0] == 0x1b && buf[1] == '[') {
        // CSI sequence
        parse_csi(buf, n, result, sizeof(result));

    } else if (n >= 2 && buf[0] == 0x1b && buf[1] == 'O') {
        // SS3 sequence
        parse_ss3(buf, n, result, sizeof(result));

    } else if (n >= 2 && buf[0] == 0x1b) {
        // Alt+key (ESC followed by a character)
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
            // ESC + UTF-8 lead byte: Alt+Option+key on some configurations.
            // Store as "alt+" followed by the raw UTF-8 character.
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
        // Unknown multi-byte sequence – store as hex
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
// Macro storage helpers
// ---------------------------------------------------------------------------

// Write a macro to a file in CSV format
void write_macro(struct macro macro, FILE *file)
{
    fprintf(file, "%s,%s,%s\n", macro.name, macro.command, macro.keybinding);
}

struct macro *macros;
int num_macros = 0;

// Read macros from a file into memory.
// Returns false if no macros are found, true otherwise.
bool read_macros(char *config_path)
{
    FILE *file = fopen(config_path, "r");
    if (!file) {
        perror("fopen: could not open macros file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file))
        num_macros++;
    rewind(file);

    macros = malloc(num_macros * sizeof(struct macro));
    if (!macros) {
        perror("malloc: could not allocate memory for macros");
        exit(EXIT_FAILURE);
    }

    int i = 0;
    while (fgets(line, sizeof(line), file)) {
        char *name       = strtok(line, ",");
        char *command    = strtok(NULL, ",");
        char *keybinding = strtok(NULL, "\n");
        if (!name || !command || !keybinding) continue;
        macros[i].name       = strdup(name);
        macros[i].command    = strdup(command);
        macros[i].keybinding = strdup(keybinding);
        i++;
    }

    fclose(file);
    return num_macros > 0;
}

void create_new_macro(char *name, char *command, char *keybinding, char *config_path)
{
    struct macro macro;
    macro.name       = strdup(name);
    macro.command    = strdup(command);
    macro.keybinding = strdup(keybinding);

    num_macros++;
    macros = realloc(macros, num_macros * sizeof(struct macro));
    if (!macros) {
        perror("realloc: could not allocate memory for macros");
        exit(EXIT_FAILURE);
    }
    macros[num_macros - 1] = macro;

    FILE *file = fopen(config_path, "a");
    if (!file) {
        perror("fopen: could not open macros file");
        exit(EXIT_FAILURE);
    }
    write_macro(macro, file);
    fclose(file);
}

void delete_macro(const char *name, char *config_path)
{
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            free(macros[i].name);
            free(macros[i].command);
            free(macros[i].keybinding);
            for (int j = i; j < num_macros - 1; j++) {
                macros[j] = macros[j + 1];
            }
            num_macros--;
            macros = realloc(macros, num_macros * sizeof(struct macro));
            if (!macros) {
                perror("realloc: could not allocate memory for macros");
                exit(EXIT_FAILURE);
            }
            FILE *file = fopen(config_path, "w");
            if (!file) {
                perror("fopen: could not open macros file");
                exit(EXIT_FAILURE);
            }
            for (int j = 0; j < num_macros; j++) {
                write_macro(macros[j], file);
            }
            fclose(file);
            return;
        }
    }
    printf("No macro found with name \"%s\".\n", name);
}

void run_macro_by_name(const char *name)
{
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            system(macros[i].command);
            return;
        }
    }
    printf("No macro found with name \"%s\".\n", name);
}

void run_macro_by_keybinding(const char *keybinding)
{
    for (int i = 0; i < num_macros; i++) {
        if (strcmp(macros[i].keybinding, keybinding) == 0) {
            system(macros[i].command);
            return;
        }
    }
    printf("No macro found for keybinding \"%s\".\n", keybinding);
}

// ---------------------------------------------------------------------------
// Rewrite the entire CSV to disk, reflecting the current in-memory macros[].
// Called after any edit that mutates an existing macro.
// ---------------------------------------------------------------------------
static void save_all_macros(const char *config_path)
{
    FILE *file = fopen(config_path, "w");
    if (!file) {
        perror("fopen: could not save macros");
        return;
    }
    for (int i = 0; i < num_macros; i++)
        write_macro(macros[i], file);
    fclose(file);
}

// ---------------------------------------------------------------------------
// Main program loop
// ---------------------------------------------------------------------------
void main_loop(char *config_path)
{
    char macro_name[32], macro_command[1024];

    system("clear");
    for (int i = 0; i < screen_width; i++)
        putchar('=');
    putchar('\n');
    switch (mode) {
        // ------------------------------------------------------------------
        case MODE_NORMAL: {
            printf("\nWhat do you want to do?\n");
            printf("[1] Insert custom macro\n");
            printf("[2] Insert preset macro\n");
            printf("[3] Run macro by name\n");
            printf("[4] Run macro by keybinding\n");
            printf("[5] List all macros\n");
            printf("[6] Delete macro\n");
            printf("[7] Clear macros\n");
            printf("[8] Edit macro by name\n");
            printf("[9] Edit macro by keybinding\n");
            printf("[10] Quit\n");
            int choice;
            printf("> ");

            // Prevent it from instantly returning on EOF
            // Because then it breaks and repeatedly gets to system("clear"), which blocks Ctrl+C,
            // leading to a broken program that cannot be interrupted with SIGINT
            if (scanf("%d", &choice) == EOF) {
                system("clear");
                exit(EXIT_SUCCESS);
            }
            clear_stdin();
            switch (choice) {
                case 1: mode = MODE_INSERT_CUSTOM;            break;
                case 2: mode = MODE_INSERT_PRESET;            break;
                case 3: mode = MODE_RUN_BY_NAME;              break;
                case 4: mode = MODE_RUN_BY_KEYBINDING;        break;
                case 5: mode = MODE_LIST_MACROS;              break;
                case 6: mode = MODE_DELETE_MACRO;             break;
                case 7: mode = MODE_CLEAR_MACROS;             break;
                case 8: mode = MODE_EDIT_MACRO_BY_NAME;       break;
                case 9: mode = MODE_EDIT_MACRO_BY_KEYBIND;    break;
                case 10: system("clear"); exit(EXIT_SUCCESS);
                default: break;
            }
            break;
        }

        // ------------------------------------------------------------------
        case MODE_INSERT_CUSTOM: {
            printf("Enter macro name (31 characters max): ");
            scanf("%31[^\n]", macro_name);
            clear_stdin();

            char *keybinding = read_keybinding();
            printf("Captured keybinding: %s\n", keybinding);

            printf("Enter macro command (1023 characters max): ");
            scanf(" %1023[^\n]", macro_command); // The format allows it to read spaces
            clear_stdin();

            create_new_macro(macro_name, macro_command, keybinding, config_path);
            free(keybinding);

            printf("Macro created successfully!\n");

            for (int i = 0; i < screen_width; i++)
                putchar('-');
            putchar('\n');

            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_INSERT_PRESET: {
            printf("Enter macro name (31 characters max): ");
            scanf("%31s", macro_name);
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
            printf("[8] Display all files in current directory with details\n"
                   "    (Move this executable to the directory where you want to use it)\n");

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
                default:
                    printf("Invalid choice.\n");
                    break;
            }

            free(keybinding);
            printf("Macro created successfully!\n");

            for (int i = 0; i < screen_width; i++)
                putchar('-');
            putchar('\n');

            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        // ------------------------------------------------------------------
        case MODE_RUN_BY_NAME: {
            printf("Enter macro name: ");
            scanf("%31s", macro_name);
            clear_stdin();
            run_macro_by_name(macro_name);

            printf("Press Enter to continue...");
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

            printf("Press Enter to continue...");
            getchar();


            mode = MODE_NORMAL;
            break;
        }

        case MODE_LIST_MACROS: {
            printf("Format: name, command, keybinding\n\n");
            for (int i = 0; i < num_macros; i++) {
                printf("%s, %s, %s\n", macros[i].name, macros[i].command, macros[i].keybinding);
            }

            if (num_macros == 0) {
                printf("(none)\n");
            }

            for (int i = 0; i < screen_width; i++)
                putchar('-');
            putchar('\n');

            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }

        case MODE_DELETE_MACRO: {
            printf("Enter macro name: ");
            scanf("%31s", macro_name);
            clear_stdin();
            delete_macro(macro_name, config_path);
            mode = MODE_NORMAL;
            break;
        }

        case MODE_CLEAR_MACROS: {
            // Remove all the macros from the macros[] array
            for (int i = 0; i < num_macros; i++) {
                free(macros[i].name);
                free(macros[i].command);
                free(macros[i].keybinding);
            }
            free(macros);
            macros = NULL;
            num_macros = 0;

            // Clear config file
            FILE *clear_file = fopen(config_path, "w");
            if (!clear_file) {
                perror("fopen: could not open macros file");
                exit(EXIT_FAILURE);
            }
            fclose(clear_file);

            printf("Macros cleared.\n");

            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }
        // ------------------------------------------------------------------
        case MODE_EDIT_MACRO_BY_NAME: {
            printf("Enter macro name to edit: ");
            char edit_name[32] = {0};
            scanf("%31[^\n]", edit_name);
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
            } else {
                int idx = found;
                printf("Current name:       %s\n", macros[idx].name);
                printf("Current command:    %s\n", macros[idx].command);
                printf("Current keybinding: %s\n\n", macros[idx].keybinding);

                // New name (blank = keep)
                char new_name[32] = {0};
                printf("New name (press Enter to keep \"%s\"): ", macros[idx].name);
                scanf("%31[^\n]", new_name);
                clear_stdin();
                if (new_name[0] != '\0') {
                    free(macros[idx].name);
                    macros[idx].name = strdup(new_name);
                }

                // New command (blank = keep)
                char new_command[1024] = {0};
                printf("New command (press Enter to keep current): ");
                scanf("%1023[^\n]", new_command);
                clear_stdin();
                if (new_command[0] != '\0') {
                    free(macros[idx].command);
                    macros[idx].command = strdup(new_command);
                }

                // New keybinding (Enter = keep)
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

            for (int i = 0; i < screen_width; i++)
                putchar('-');
            putchar('\n');
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
                if (strcmp(macros[i].keybinding, search_kb) == 0) {
                    found = i;
                    break;
                }
            }
            free(search_kb);

            if (found == -1) {
                printf("No macro found for that keybinding.\n");
            } else {
                int idx = found;
                printf("Current name:       %s\n", macros[idx].name);
                printf("Current command:    %s\n", macros[idx].command);
                printf("Current keybinding: %s\n\n", macros[idx].keybinding);

                // New name (blank = keep)
                char new_name[32] = {0};
                printf("New name (press Enter to keep \"%s\"): ", macros[idx].name);
                scanf("%31[^\n]", new_name);
                clear_stdin();
                if (new_name[0] != '\0') {
                    free(macros[idx].name);
                    macros[idx].name = strdup(new_name);
                }

                // New command (blank = keep)
                char new_command[1024] = {0};
                printf("New command (press Enter to keep current): ");
                scanf("%1023[^\n]", new_command);
                clear_stdin();
                if (new_command[0] != '\0') {
                    free(macros[idx].command);
                    macros[idx].command = strdup(new_command);
                }

                // New keybinding (Enter = keep)
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

            for (int i = 0; i < screen_width; i++)
                putchar('-');
            putchar('\n');
            printf("Press Enter to continue...");
            getchar();

            mode = MODE_NORMAL;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void)
{
    // Build config path  ~/.config/hackathon/macros.csv
    char *home_dir    = getenv("HOME");
    char *config_path = malloc(strlen(home_dir) + strlen("/.config/hackathon/macros.csv") + 1);
    sprintf(config_path, "%s/.config/hackathon/macros.csv", home_dir);

    // Ensure ~/.config exists
    char *config_dir = malloc(strlen(home_dir) + strlen("/.config") + 1);
    sprintf(config_dir, "%s/.config", home_dir);
    if (mkdir(config_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir: could not create ~/.config");
        free(config_dir);
        free(config_path);
        exit(EXIT_FAILURE);
    }
    free(config_dir);

    // Ensure ~/.config/hackathon exists
    char *hackathon_dir = malloc(strlen(home_dir) + strlen("/.config/hackathon") + 1);
    sprintf(hackathon_dir, "%s/.config/hackathon", home_dir);
    if (mkdir(hackathon_dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir: could not create ~/.config/hackathon");
        free(hackathon_dir);
        free(config_path);
        exit(EXIT_FAILURE);
    }
    free(hackathon_dir);

    // Create the config file if it doesn't exist yet
    FILE *file = fopen(config_path, "a");
    if (!file) {
        perror("fopen: could not open macros file");
        free(config_path);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    // Print a separator scaled to the terminal width
    struct winsize window_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);
    screen_width = window_size.ws_col;

    // Load existing macros
    bool has_macros = read_macros(config_path);
    if (!has_macros)
        mode = MODE_INSERT_CUSTOM;

    while (true)
        main_loop(config_path);

    free(config_path);
    return 0;
}
