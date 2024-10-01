/*** includes ***/

#define _DEFAULT_SOURCE // Enable default set of features provided by glibc
#define _BSD_SOURCE // Enable BSD extensions in glibc
#define _GNU_SOURCE // Enables all GNU extensions

#include <ctype.h> // Access iscntrl()
#include <errno.h> // Access errno, EAGAIN
#include <fcntl.h> // Access open(), O_RDWR, O_CREAT
#include <stdio.h> // Access printf(), perror(), sscanf(), snprintf(), FILE, fopen(), getline(), vsnprintf()
#include <stdarg.h> // Access va_list, va_start(), va_end()
#include <stdlib.h> // Access atexit(), exit(), realloc(), free(), malloc()
#include <string.h> // Acess memcpy(), strlen(), strdup(), memmove(), strerror(), strstr(), memset(), strrchr(), strcmp(), strncmp()
#include <sys/ioctl.h> // Access ioctl(), TIOCGWINSZ, struct winsize
#include <sys/types.h> // Access ssize_t
#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <time.h> // Access time_t, time()
#include <unistd.h> // Access read(), STDIN_FILENO, write(), ftruncate(), close()

/*** defines ***/

#define SIMPLE_TEXT_EDITOR_VERSION "0.0.1" // Version number for welcome message display
#define TAB_STOP_LENGTH 8 // The length of a tab stop
#define CONFIRM_QUIT_TIMES 3 // Require user to to quit 3 times to quit without saving
#define CTRL_KEY(k) ((k) & 0x1f) // CTRL keypress macro
#define HL_HIGHLIGHT_NUMBERS (1<<0) // Flag bit for numbers
#define HL_HIGHLIGHT_STRINGS (1<<1) // Flag bit for strings
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0])) // Constant to store the length of the HLDB array

// Keys that move the cursor or page in the editor
// Represent keys with large integer values out of 
// range for char to avoid conflicting with ordinary keypresses
enum editorKey {
    BACKSPACE = 127, // Actual ASCII value
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

// Contain the possible values of the erow highlight array
enum editorHighlight {
    HL_NORMAL = 0,
    HL_STRING,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_NUMBER,
    HL_MATCH
};

/*** data ***/

// Contain all the syntax highlighting information for a particular filetype
struct editorSyntax {
    // Name of the filetype that will be displayed to the user
    char *filetype;

    // Array of strings where each string contains a 
    // pattern to match a filename against
    char **filematch;

    // Null terminated array of strings
    // Each string contains a keyword
    char **keywords;

    // Single-line comment pattern 
    char *singleline_comment_start;

    // Multi-line comment start and end ("/*" and "*/")
    char *multiline_comment_start;
    char *multiline_comment_end;

    // Bit field that contains flags for whether to highlight
    // numbers and whether to highlight strings for the filetype
    int flags;
};

// Data type for storing a row of text in the editor
typedef struct erow {
    // Track row index
    int idx;

    // Length
    int size;

    // Contains size of the contents of render
    int rsize;

    // Allocated char data
    char *chars;

    // Contains the actual chars to draw on the screen for the row
    // Needed to render nonprintable control chars i.e. tabs
    char *render;

    // Store the highlighting of each line in an array
    // Need to know the highlighting for each row before displaying
    // and then re-highlight a line whenever it gets changed
    // Stores integers in the range of 0-255, each value corresponds
    // to a char in render and will indicate if the char is part of the
    // string or a comment, number, and so on
    unsigned char *hl;

    // Boolean variable to check if the line is part of 
    // an unclosed multi-line comment
    int hl_open_comment;
} erow;

// Global struct to contain editor state
struct editorConfig {
    // Cursor's x and y position
    // cx is an index into the chars field of an erow
    int cx, cy;

    // Index into into the render field
    // If there are no tabs on the current line, rx = cx
    // If there are tabs, rx > cx by however many extra spaces those tabs take up when rendered
    int rx;

    // Track of what row of the file the user is currently scrolled to
    int rowoff;

    // Track of what column of the file the user is currently scrolled to
    int coloff;

    // Number of current rows in terminal screen
    int screenrows;

    // Number of current columns in terminal screen
    int screencols; 

    // Store original terminal attributes
    struct termios orig_termios; 

    // Number of displayed lines 
    int numrows;

    // Pointer to array of erow structs to store multiple lines
    erow *row;

    // Stores the filename when a file is opened 
    char *filename;

    // Stores current status message 
    // Used for displaying messages to the user and prompt for input
    char statusmsg[80];

    // Timestamp for when the status message is set
    time_t statusmsg_time;

    // Track whether the text loaded in the editor differs from 
    // what is in the file since opening or saving
    int dirty;

    // Pointer to the current editor syntax
    // Null means there's no filetype for the current file,
    // so no syntax highlighting should be done
    struct editorSyntax *syntax;
};

struct editorConfig E;

/*** filetypes ***/

// List of file extensions
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };

// To differentiate between two types of keywords,
// terminate common type keywords with a pipe char
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

// Array of editorSyntax structs
// HLDB = "highlight database"
struct editorSyntax HLDB[] = {
    // C
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

/*** function prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

// Error handling
void die(const char *s) {
    // Clear entire screen and reposition cursor
    // to avoid printing error message at cursor's 
    // recent position when error occurs during rendering
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    // Print error and exit program
    perror(s);
    exit(1);
}

// Restore original terminal attributes after enabling raw mode and exiting program
void disableRawMode() {
    // Discard any unread input before restoring terminal attributes
    if (tcsetattr(STDERR_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

// Disable the echoing of input characters in the terminal (raw mode)
// Set terminal's attributes by reading current attributes,
// modifying it, and writing new terminal attributes back out
void enableRawMode() {
    // Store original terminal attributes into global struct
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    // disableRawMode called when program exits
    atexit(disableRawMode);   

    // Declare new struct which will record all the I/O attributes of a terminal
    struct termios raw = E.orig_termios;

    // Turn off break condition that can cause SIGINT to be sent
    // Turn off translating carriage returns to newlines
    // Turn off parity checking (might be turned off by default)
    // Turn off stripping of 8th bit (might be turned off by default)
    // Turn off XON/XOFF to avoid resuming/pausing data transmission (disable Ctrl-Q/S)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Turn off all output processing (avoid translating newlines into carriage returns + newline)
    raw.c_oflag &= ~(OPOST);

    // Set character size to 8 bits per byte
    raw.c_cflag |= (CS8);

    // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary
    // Bitwise-AND inverted ECHO bits with the flag's field, which forces the fourth bit to be 0 
    // and causes every other bit to retain its current value.
    // Turn off canonical mode, read input byte-by-byte instead of line-by-line
    // Turn off IEXTEN to disable Ctrl-V (or O) to avoid stopping inputs
    // Turn off sending SIGINT/SIGSTP to avoid terminating/suspending (disable Ctrl-C/Z)
    // NOTE: ICANON/IEXTEN are local flags in c_lflag field
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Set a timeout for read()
    // Set minimum number of bytes of input needed to 0 so read() returns when there's any input to be read
    // Set max wait time to 1/10 second before read() returns
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // Set the modified terminal attributes for standard input
    // TCSAFLUSH for applying changes after flushing input buffer to discard any unread input
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// Read and return keypress inputs
int editorReadKey() {
    int nread;
    char c;

    // Keep reading 1 byte from standard input into variable c until there are no more bytes to read
    // read() returns number of bytes read, will return 0 when it reaches the end of a file
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // Check if escape char is read
    if (c == '\x1b') {
        // Declare buffer to store escape sequence chars
        char seq[3];
        
        // Read two more bytes into a buffer
        // If either of the reads timeout (around 0.1s), then assume
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        // Look to see if escape sequence is a special [ or O char
        // Depending on terminal emulator or OS, escape sequences can be <esc>[(int)~ or <esc>O(char)
        if (seq[0] == '[') {
            // Check if digit byte is between 0 and 9
            // If it is, read the next byte and check for a tilde
            // If it is a tilde, return the corresponding key if it's 1, 3, 4, 5, 6, 7, or 8
            // Otherwise, return corresponding key if it's A, B, C, D, H, or F
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        // Return escape char, if char isn't recognizable
        return '\x1b';
    } else {
        return c;
    }
}

// Get the cursor position for displaying
int getCursorPosition(int *rows, int *cols) {
    // Buffer to hold escape sequence response for parsing
    char buf[32];
    
    unsigned int i = 0;

    // Get cursor position
    // Response would be in the form: <esc>[24;80R (or something similar)
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    // Keep reading characters until 'R' (the ending character from the previous write response)
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    // Assign 0 byte to final byte of buffer
    // NOTE: Strings expected to end with 0 byte
    buf[i] = '\0';

    // If buffer doesn't contain escape character of '[' then fail
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;

    // Parse response in buf, pass a pointer to third character in buf to skip escape character and '['
    // String is the form: "integer;integer" and pass it to rows and cols variables
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    
    return 0;
}

// Get the current terminal size in terms of number of rows and columns
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // On success, put the number of columns and rows of the terminal to the winsize struct
    // On failure or if the returned values are 0, return -1
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // ioctl() not guaranteed to get window size on every system
        // Move cursor to the bottom right corner, so we can measure size 
        // using getCursorPosition() later
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        
        // Get the window size 
        return getCursorPosition(rows, cols);
    } else {
        // Set int references
        *cols = ws.ws_col;
        *rows = ws.ws_row;

        return 0;
    }
}

/*** syntax highlighting ***/

// Checks if a character is considered a separator character
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// Highlight the characters in an erow
void editorUpdateSyntax(erow *row) {
    // Reallocate the needed memory since this might be a new row 
    // or the row might be bigger than the last time we highlighted it
    // The size of the hl array is the same as the render array
    row->hl = realloc(row->hl, row->rsize);

    // Set all of the chars to HL_NORMAL by default
    // before we loop through the chars and set the digits
    // Any unhighlighted chars will have a HL_NORMAL value
    memset(row->hl, HL_NORMAL, row->rsize);

    // If filetype is set, return immediately after setting 
    // the line to the default highlighting
    if (E.syntax == NULL) return;

    // Make an alias for the keywords array in syntax struct 
    char **keywords = E.syntax->keywords;

    // Alias for the comment syntax for the file
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    // Set the length of the string or 0 if the string is null
    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    // Track whether the previous char was a separator
    // Set it to 1 (true) since we consider the beginning
    // of the line to be a separator
    int prev_sep = 1;

    // Keep track of whether we're currently inside a string
    int in_string = 0;

    // Boolean to track if we're currently inside a multi-line
    // comment (not used for single-line comments)
    // Set to true if the previous row has an unclosed multi-line comment
    // If that's the case, then the current row will start out 
    // being highlighted as a multi-line comment
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;

    // Loop the the characters and set each char
    // to a corresponding highlight integer/value
    while (i < row->rsize) {
        // Get the char in the render string
        char c = row->render[i];

        // Set the highlight type of the previous char
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        // Check if the comment line has a length and if we're not 
        // in a string and if we're not in a multi-line comment
        // If checks pass, use strncmp() to check if this character is the start 
        // of a single-line comment, if so, set the memory block for the whole 
        // rest of the line with HL_COMMENT and break out of the syntax highlighting loop
        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        // Check if multi-line comment start and end are non-null
        // strings of length greater than 0 and check if we're not in a string since having a "/*" char
        // inside a string doesn't usually start a comment
        // Otherwise, check if we're at the start of a multi-line comment
        if (mcs_len && mce_len && !in_string) {
            // Check if we're currently in multi-line comment
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;

                // Check if we're at the end of a multi-line comment 
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    // Highlight the whole multi-line end comment string
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);

                    // Consume entire multi-line end comment string
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    // If not at the end of the comment, consume
                    // the current char we already highlighted
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                // Highlight the whole multi-comment start string
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);

                // Consume entire multi-line start comment string
                i += mcs_len;

                // We're in a multi-line comment, so set the flag to true
                in_comment = 1;
                continue;
            }
        }

        // Check if the strings should be highlighted for the current filetype
        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            // If we're in a string, then keep highlighting the 
            // current character as a string until we hit the closing quote
            // If we're not in a string, check if we’re at the beginning 
            // of one by checking for a double- or single-quote, if we are,
            // then store quote in in_string, highlight it with HL_STRING, and consume it
            if (in_string) {
                row->hl[i] = HL_STRING;

                // If we’re in a string and the current character is a backslash, 
                // and there’s at least one more character in that line that comes 
                // after the backslash, then we highlight the character that comes 
                // after the backslash with HL_STRING and consume it 
                // Increment i by 2 to consume both characters at once.
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                // Check if current char is the closing quote
                // If so, reset in_string flag to 0
                if (c == in_string) in_string = 0;

                // Since we highlighted the current character, we have to 
                // consume it by incrementing i and continuing out of the current loop iteration
                // Set prev_sep to 1 so that if we’re done highlighting the string, 
                // the closing quote is considered a separator
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        // Check if the numbers should be highlighted for the current filetype
        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            // Digits/numbers/decimals
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        // Keywords require separator before and after the keyword
        // Otherwise the keyword substring in other strings would be highlighted 
        // Check to make sure a separator came before the keyword prior to 
        // looping through each possible keyword 
        if (prev_sep) {
            int j;
            
            // Loop through each keyword
            for (j = 0; keywords[j]; j++) {
                // Store the length of the keyword
                int klen = strlen(keywords[j]);

                // If it's a secondary keyword, decrement
                // length to account for the pipe char
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;
                
                // Check if keyword exists at current position in text
                // and check if a separator char comes after the keyword
                // Since null char is considered a separator, this works 
                // at the very end of the line
                if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
                    // We have a keyword to highlight 
                    // Highlight whole keyword at once depending on the value of kw2
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);

                    // Consume entire keyword by incrementing i by length of keyword
                    i += klen;

                    // Break because we're in an inner loop, so break before
                    // continuing the outer loop 
                    break;
                }
            }

            // Check if the loop was broken out by seeing
            // if it got to the terminating null value
            // If it was broken out of, continue
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        // If no char was highlighted, set the current char
        // according to whether it's a separator or not
        prev_sep = is_separator(c);
        i++;
    }

    // Set boolean flag to whether or not the user commmented out a
    // part of the file
    int changed = (row->hl_open_comment != in_comment);

    // Set the value of the current row’s hl_open_comment to 
    // whatever state in_comment is after processing the entire row
    row->hl_open_comment = in_comment;

    // Check if line got changed and and if there is a next line in the file
    if (changed && row->idx + 1 < E.numrows)
        // Highlight the commented out row as a multi-line comment
        // Because editorUpdateSyntax() keeps calling itself with the next line, 
        // the change will continue to propagate to more and more lines until 
        // one of them is unchanged, at which point we know that all the lines 
        // after that one must be unchanged as well.
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

// Maps the highlight values to corresponding color codes
int editorSyntaxToColor(int hl) {
    switch (hl) {
        // Numbers are highlighted red
        case HL_NUMBER: return 31;

        // Strings are highlighted magenta
        case HL_STRING: return 35;

        // Comments are highlighted as cyan
        case HL_COMMENT:
        case HL_MLCOMMENT: return 36;

        // Specify two keyword types
        // Actual keywords in one color (yellow) and 
        // common type names in another color (green)
        case HL_KEYWORD1: return 33;
        case HL_KEYWORD2: return 32;

        // Search results are highlighted blue
        case HL_MATCH: return 34;

        // Default is white
        default: return 37;
    }
}

// Match the current filename to one of the filematch fields in the HLDB
// to the set the appropriate syntax highlighting for a filetype
void editorSelectSyntaxHighlight() {
    // Set to null so if there's no matches, there's no filename and filetype
    E.syntax = NULL;

    // If there's no filename, return immediately
    if (E.filename == NULL) return;
    
    // Get the pointer to the extension part of the filename
    // If there is none, this will be null
    char *ext = strrchr(E.filename, '.');
    
    // Loop through each editorSyntax struct in the HLDB array
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        // Get pointer to the struct
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;

        // Loop through each pattern in the filematch array
        while (s->filematch[i]) {
            // Check if there's a file extension
            int is_ext = (s->filematch[i][0] == '.');
            
            // If the pattern starts with a '.', we know it's a file extension
            // and we see if the filename ends with that extension using strcmp()
            // If it's not a file extension pattern, the we check to see if the 
            // pattern exists anywhere in the filename using strstr()
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
                // Set syntax highlight to the current syntax struct
                E.syntax = s;

                // Rehighlight the entire file after setting the syntax highlighting
                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }
            
            i++;
        }
    }
}

/*** row operations ***/

// Calculate the value of the horizontal render position from the cursor position
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;

    // Loop through all the chars to the left of the cursor position (cx)
    // and figure out how many spaces each tab takes up
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            // rx % TAB_STOP_LENGTH for how many columns we're to the right of the last tab stop
            // TAB_STOP_LENGTH - 1 for how many columns we're to the left of the next tab stop
            // Add to rx to get to the left of the next tab stop
            rx += (TAB_STOP_LENGTH - 1) - (rx % TAB_STOP_LENGTH);
        
        // Go to the next tab stop
        rx++;
    }

    return rx;
}

// Calculate the cursor position from the horizontal render position
int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;

    // Loop through the chars in the string
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            // rx % TAB_STOP_LENGTH for how many columns we're to the right of the last tab stop
            // TAB_STOP_LENGTH - 1 for how many columns we're to the left of the next tab stop
            // Add to rx to get to the left of the next tab stop
            cur_rx += (TAB_STOP_LENGTH - 1) - (cur_rx % TAB_STOP_LENGTH);

        // Go to the next tab stop
        cur_rx++;

        // When the current render position hits the given render position,
        // return the current cursor position
        if (cur_rx > rx) return cx;
    }

    // Return in the case that the caller provided a 
    // render position that is out of range
    return cx;
}

// Use the string of an erow to fill the contents of the render string
void editorUpdateRow(erow *row) {
    int j;
    int tabs = 0;
    int idx = 0;

    // Count the number of tabs contained in the string
    // to know how much memory to allocate for render
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    // Free any used memory from the render buffer in order to update it
    free(row->render);

    // Allocate memory to store render string
    row->render = malloc(row->size + tabs * (TAB_STOP_LENGTH - 1) + 1);

    // Copy chars stored in the erow to the render buffer
    // Render tabs as multiple space chars
    for (j = 0; j < row->size; j++) {
        // If the char in the string is a tab, render it as a space
        // and append spaces until we get to a tab stop (a column divisible by 8)
        // Otherwise, copy the char directly to render
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP_LENGTH != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }

    // Terminate render string with null char
    row->render[idx] = '\0';

    // idx contains the number of chars we copied into render
    // Assign it to render size
    row->rsize = idx;

    // Set the highlighting of the row after the render is updated
    editorUpdateSyntax(row);
}

// Inserts a row at the specified index 
void editorInsertRow(int at, char *s, size_t len) {
    // If the index is not within the row, then exit the function
    if (at < 0 || at > E.numrows) return;

    // Reallocate memory to resize array to append new line
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // Shift the rows to make room at the specified index for the new row
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    // Increment the idx of each row whenever a row is inserted into the file
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    // Set to row's index in the file at the time it's inserted
    E.row[at].idx = at;

    // Set row size to length of the line
    E.row[at].size = len;

    // Allocate memory for the line 
    E.row[at].chars = malloc(len + 1);

    // Store line to chars field which points to the allocated memory
    memcpy(E.row[at].chars, s, len);

    // Terminate string with null char
    E.row[at].chars[len] = '\0';

    // Initialize render and highlighting
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;

    // Update render string
    editorUpdateRow(&E.row[at]);

    // Update number of rows
    E.numrows++;

    // Increment in each row operation that makes a change to the text
    E.dirty++;
}

// Frees the memory owned by the erow being deleted
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

// Deletes a row
void editorDelRow(int at) {
    // If the index is not within the row, then exit the function
    if (at < 0 || at >= E.numrows) return;

    // Free the memory of the row being deleted
    editorFreeRow(&E.row[at]);

    // Shift the row structs in the array to the left to accommodate for the deleted 
    // row struct by overwriting it with the row structs that come after it
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));

    // Decrement the idx of each row whenever a row is removed from the file
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;

    // Decrement the number of rows
    E.numrows--;

    // Increment in each row operation that makes a change to the text
    E.dirty++;
}

// Inserts a single char into an erow at a given position
void editorRowInsertChar(erow *row, int at, int c) {
    // Validate the index we want to insert into
    // Allowed to go one char past the end of the string, so we insert at the end
    if (at < 0 || at > row->size) at = row->size;

    // Allocate one more byte for the chars of the erow
    // Add 2 because we need to make space for the null byte
    row->chars = realloc(row->chars, row->size + 2);

    // Move the char at the index we are inserting to the next index
    // Using memmove() because we are handling overlapping memory areas
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    // Increment size of array 
    row->size++;

    // Assign character at the given position in the array
    row->chars[at] = c;

    // Update the render string to update the new row content
    editorUpdateRow(row);

    // Increment in each row operation that makes a change to the text
    E.dirty++;
}

// Appends a string to the end of a row
void editorRowAppendString(erow *row, char *s, size_t len) {
    // After appending, the row's new size is row->size + len + 1
    // Allocate that much memory for the string to be stored in the row
    row->chars = realloc(row->chars, row->size + len + 1);

    // Copy the given string to the end of the current string content of the row
    memcpy(&row->chars[row->size], s, len);

    // Increase the row size by the length of the appended string
    row->size += len;

    // Append null byte at the end of the new string
    row->chars[row->size] = '\0';

    // Update the render string to update the new row content
    editorUpdateRow(row);

    // Increment in each row operation that makes a change to the text
    E.dirty++;
}

// Deletes a character in an erow
void editorRowDelChar(erow *row, int at) {
    // If the index is not within the row, then exit the function
    if (at < 0 || at >= row->size) return;

    // Shift the chars to the left to accommodate for the deleted char
    // by overwriting it with the chars that come after it
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);

    // Decrement row size
    row->size--;

    // Update the render string to update the new row content
    editorUpdateRow(row);

    // Increment in each row operation that makes a change to the text
    E.dirty++;
}

/*** editor operations ***/

// Insert a character in the position that the cursor is at
void editorInsertChar(int c) {
    // If the cursor is on the tilde line after the end of the file,
    // then we append a new row to the file before inserting a char
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }

    // Insert char at the cursor's position
    editorRowInsertChar(&E.row[E.cy], E.cx, c);

    // After inserting, move the cursor forward to allow
    // the next char the user inserts to go after the inserted char
    E.cx++;
}

// Inserts a new line
void editorInsertNewline() {
    // If the cursor is at the start of a line,
    // then insert a blank row before the line the cursor is on
    // Otherwise, split the line the cursor is on into two rows
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        // Get the current row
        erow *row = &E.row[E.cy];

        // Insert the row's chars right of the cursor to 
        // create a new row after the current row
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);

        // Reassign pointer since realloc() might move 
        // around and invalidate the pointer
        row = &E.row[E.cy];

        // Truncate the size of the current row by setting
        // size to the position of the cursor
        row->size = E.cx;

        // Append null byte at the end of the current row 
        // since it was truncated
        row->chars[row->size] = '\0';

        // Update the render string to update the current row content
        editorUpdateRow(row);
    }

    // Move the cursor to the beginning of the row
    E.cy++;
    E.cx = 0;
}

// Deletes the character left of the cursor
void editorDelChar() {
    // If cursor is at the end of the file, there's nothing
    // to delete, so exit the function
    if (E.cy == E.numrows) return;

    // If cursor is at the beginning of the first line,
    // there's nothing to do, so exit the function
    if (E.cx == 0 && E.cy == 0) return;

    // Get the row where the cursor is on
    erow *row = &E.row[E.cy];

    // If there's a char left of the cursor,
    // proceed to delete it and move the cursor one to the left
    // Otherwise if the cursor is at the beginning of ar row, 
    // set the cursor to the end of the preceding line, append the current 
    // line to the end of the preceding line, and delete the current line
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/*** file i/o ***/

// Converts erow struct array into a single string
// that will be written out to a file
char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;

    // Add all of the lengths of row of text
    // Add 1 to each row to account newline char 
    // that will be added at the end of each line
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;

    // Save the total length to tell caller how long the string is
    *buflen = totlen;

    // Allocate space for a buffer based on the total string length
    char *buf = malloc(totlen);
    
    // Set a pointer to the buffer, so the buffer maintains
    // its pointer at the start and to be used later
    // to free the allocated memory
    char *p = buf;

    // Loop through the rows and copy the contents to the 
    // end of the buffer and append a newline char after each row
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    // Return the buffer, expect caller to free memory
    return buf;
}

// Opens and reads a files from disk
void editorOpen(char *filename) {
    // Free used memory to update the filename
    free(E.filename);

    // Duplicate the filename and store it as a global state
    E.filename = strdup(filename);

    // Set appropriate syntax highlighting for the file type
    editorSelectSyntaxHighlight();

    // Opens the file that the user passed as the arg for reading
    FILE *fp = fopen(filename, "r");

    // If fopen failed to open the file, error out
    if (!fp) die("fopen");
    
    // Pointer that points to the buffer where the read message will be stored
    char *line = NULL;

    // Size of the buffer pointed to by the message pointer
    size_t linecap = 0;

    // Length of the message
    ssize_t linelen;

    // Read lines from the file and store to the allocated buffer
    // Updates pointer and allocates necessary memory
    // Check if the returned value is not the end of the file 
    // -1 indicates there are no more lines to read
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        // Strip off newline or carriage return at the end of the line 
        // before copying it to the erow since each erow represents one line of text
        // there's no reason to store a newline character at the end of each one
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;

        // Append line to erow struct array
        editorInsertRow(E.numrows, line, linelen);
    }

    // Free the memory used by the buffer 
    free(line);

    // Close the file since we're done reading
    fclose(fp);

    // File is unchanged when opened
    E.dirty = 0;
}

// Write a string to disk
// Note: string is returned by editorRowsToString()
void editorSave() {
    // If it's a new file, prompt the user for a filename to save as
    if (E.filename == NULL) {
        // Prompt the user for filename
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        
        // If user aborts the save, then display a message
        // indicating that action and exit the function
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }

        // Set appropriate syntax highlighting for the file type
        editorSelectSyntaxHighlight();
    }

    int len;

    // Get the string that will be saved to disk
    char *buf = editorRowsToString(&len);
    
    // Open the file for reading and writing from the given filename if it exists
    // Create a new file if it doesn't exist 
    // Note: 0644 is file permissions for the owner to read and write the file
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);

    // If there's an error opening the file, close the file
    if (fd != -1) {
        // Set the file's size to the specified length 
        // If the file is larger, then the data is cut off at the end of the file
        // If the file is smaller, then it will add 0 bytes at the end to get the length
        // If truncation fails, we expect write() to return the number of specified bytes
        if (ftruncate(fd, len) != -1) {
            // Write the string to the file destination
            if (write(fd, buf, len) == len) {
                // Close the file
                close(fd);

                // Free the memory used by the buffer
                free(buf);

                // Once saved, file isn't "dirty" anymore
                E.dirty = 0;

                // Notify user that the save succeeded
                editorSetStatusMessage("%d bytes written to disk", len);

                return;
            }
        }

        // Close the file
        close(fd);
    }

    // Free the memory used by the buffer
    free(buf);

    // On error, notify the user that the save failed
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*** find ***/

// Callback function that searches through all the rows in the file and if a 
// row contains the query string, move the cursor to the match
void editorFindCallback(char *query, int key) {
    // Store the index of the row that the last match was on
    // It will be -1 if there was no last match
    static int last_match = -1;

    // Store the direction of the search
    // 1 for searching forward and -1 for searching backward
    static int direction = 1;

    // Use to know which line's hl needs to be restored
    static int saved_hl_line;

    // Save the original contents of hl
    static char *saved_hl = NULL;

    // If there's a line to restore, then copy the saved hl
    // to the row's hl and deallocate the saved hl array
    if (saved_hl) {
        // Restore the row's hl 
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);

        // Free the memory and set the pointer to null
        free(saved_hl);
        saved_hl = NULL;
    }

    // If the user's keypress is either enter or escape,
    // then they are attempting to leave search mode, so quit the function
    // last_match is reset to -1 unless an arrow key was pressed 
    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    // If there's no last match, we are searching forward
    if (last_match == -1) direction = 1;

    // Store the index of the current row we're searching
    int current = last_match;
    
    int i;
    
    // Loop through all the rows in the file
    for (i = 0; i < E.numrows; i++) {
        // Move the index in the direction being searched
        current += direction;

        // If the current index is past the top of the file, set it to the end of the file
        // If the current index is at the end of the file, reset it back to the top of the file
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        // Get the row
        erow *row = &E.row[current];

        // Check if the query is a substring of the current row
        // Returns null if there's no match or a pointer to the matching
        // substring if there's a match
        char *match = strstr(row->render, query);

        // If there's a match, move the cursor to the position
        // where the matching substring is 
        if (match) {
            // Set the last match index to the current index that we searched
            last_match = current;

            // Set cursor y position to the current index we searched
            E.cy = current;

            // Set the cursor position based on the render position
            // Subtract the render pointer from the match pointer
            // since it's already a pointer to the render string
            E.cx = editorRowRxToCx(row, match - row->render);

            // Scroll to the bottom of the file, so editorScroll() 
            // will scroll up on the next refresh where the matching 
            // line will be at the top of the screen
            E.rowoff = E.numrows;

            // Set the hl line we need to save to the current 
            saved_hl_line = current;

            // Allocate memory for the saved hl array
            // NOTE: This memory is guaranteed to be freed
            // because when the user closes the search prompt
            // editorPrompt() calls this function so hl will
            // be restored before it editorPrompt() returns
            saved_hl = malloc(row->rsize);

            // Copy over the row's hl to the saved hl array
            memcpy(saved_hl, row->hl, row->rsize);

            // Set the matched substring to HL_MATCH
            // match - row->render is the index into render of the match, 
            // so we use that as our index into hl
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

// Prompts the user for a search query to search in 
// the file for the user's matching query
void editorFind() {
    // Store the user's current cursor position
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    // Prompt the user for a query string to search
    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

    // If the search isn't canceled, free the memory used 
    // by pointer pointing to the user's query input
    // Otherwise, reset the cursor back to where the user started the search
    if (query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff; 
    }
}

/*** append buffer ***/

// Define dynamic string type
// Consists of a pointer to our buffer in memory and a length
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0} // Represents empty buffer, acts as a constructor to abuf

// Append to string to abuf
void abAppend(struct abuf *ab, const char *s, int len) {
    // Allocate memory (size of current string + size of appended string) 
    // to hold new string, realloc() will either extend size 
    // of memory block already allocated or free current memory
    // and allocate new memory big enough to hold new string
    char *new = realloc(ab->b, ab->len + len);

    // If allocation fails, exit function
    if (new == NULL) return;

    // Copy string after end of current data in buffer
    memcpy(&new[ab->len], s, len);

    // Update pointer and length of abuf to new values
    ab->b = new;
    ab->len += len;
}

// Destructor to deallocate memory used by abuf
void abFree(struct abuf *ab) {
    // Deallocate to avoid memory leaks
    free(ab->b);
}

/*** output ***/

// Enable vertical scrolling
void editorScroll() {
    // Initialize render position
    E.rx = 0;

    // Check if the cursor is within a row
    if (E.cy < E.numrows) {
        // Calculate render position using the current
        // cursor x position and the row at that y position
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    // Check if the cursor is above the visible window
    // If it is, scroll up to the cursor
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    
    // Check if the cursor is past the bottom of the visible window
    // If it is, scroll down to the cursor 
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    // Check if the cursor is left of the visible window
    // If it is, scroll lef to the cursor
    // NOTE: rx is being used since scrolling should take into account
    // the chars that are actually rendered and rendered position of the cursor
    if (E.rx < E.coloff) {
        E.coloff = E.cx;
    }

    // Check if the cursor is horizontally past the visible window
    // If it is, scroll right to the cursor
    // NOTE: rx is being used since scrolling should take into account
    // the chars that are actually rendered and rendered position of the cursor
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

// Draw row of tildes (similar to vim)
void editorDrawRows(struct abuf *ab) {
    int y;

    // Draw tildes based on current number of rows in terminal
    for (y = 0; y < E.screenrows; y++) {
        // Get row of the file we want to display
        int filerow = y + E.rowoff;

        // Check if we're drawing a row that comes after the end of the text buffer
        // Otherwise, we're drawing a row that's part of the text buffer
        if (filerow >= E.numrows) {
            // Display welcome message a third of the way down the screen
            // Displays when the user starts the program with no args (when text buffer is empty)
            if (E.numrows == 0 && y == E.screenrows / 3) {
                // Buffer for welcome message
                char welcome[80];

                // Format welcome message string and store it in buffer
                int welcomelen = snprintf(welcome, sizeof(welcome), 
                    "Simple text editor -- version %s", SIMPLE_TEXT_EDITOR_VERSION);

                // Truncate string length if terminal is too small to fit the message
                if (welcomelen > E.screencols) welcomelen = E.screencols;

                // Divide screen width in half and subtract half of string's length
                // Tells how far from left edge of screen to start printing string for centering
                int padding = (E.screencols - welcomelen) / 2;

                // Fill empty left space with space chars
                // First character should be a tilde
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);

                // Append welcome message to abuf 
                abAppend(ab, welcome, welcomelen);
            } else {
                // Output tilde in the row
                abAppend(ab, "~", 1);
            }
        } else {
            // Get the length (the render length specifically) of the string
            // Subtract the number of characters that are to the left of 
            // offset from the length of the row
            int len = E.row[filerow].rsize - E.coloff;

            // len can be negative which means the user scrolled horizontally
            // past the end of the line, in that case, set len to 0 so that
            // nothing is displayed on that line
            if (len < 0) len = 0;

            // If the length is beyond the screen's columns, set it to the
            // the number of columns displayed on screen
            if (len > E.screencols) len = E.screencols;

            // Get the pointer to the render string for displaying on screen
            // To display each row at the column offset, E.coloff is used 
            // an index for the chars of each erow displayed
            char *c = &E.row[filerow].render[E.coloff];

            // Get the highlight array for the row 
            unsigned char *hl = &E.row[filerow].hl[E.coloff];

            // Keep track of the current text color as we loop through the chars
            int current_color = -1;

            // Loop through the characters in the render string 
            int j;
            for (j = 0; j < len; j++) {
                // If it's a control char, translate it into a printable char
                // Else if it's a normal char, set the text color to white
                // Otherwise, set the text color of the char depending
                // on the type of char it is
                if (iscntrl(c[j])) {
                    // Add the ctrl char to a @ char 
                    // If it's not in the alphabetic range, replace it with
                    // a ? char
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    
                    // Switch to inverted colors before printing the translated
                    // char, then turn off inverted colors again
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);

                    // The <esc>[m  turns off all text formatting including colors
                    // Print the esc sequence for the current color afterwards
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        abAppend(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        // m command inverts colors
                        // Set the text color back to default white (9 = white)
                        abAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }

                    // Append the char
                    abAppend(ab, &c[j], 1);
                } else {
                    // Get the corresponding integer for the type of char
                    // that will be highlighted 
                    int color = editorSyntaxToColor(hl[j]);

                    if (color != current_color) {
                        // Set the current color to the intended 
                        // color that the char will be highlighted
                        current_color = color;

                        // Write the escape sequence into a buffer
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);

                        // Append the color code escape sequence buffer
                        abAppend(ab, buf, clen);
                    }

                    // Append the char
                    abAppend(ab, &c[j], 1);
                }
            }

            // Reset the text color of the characters
            abAppend(ab, "\x1b[39m", 5);
        }

        // Clear each line as they're redrawn instead 
        // of clearing entire screen before each refresh
        // Put <esc>[K sequence at the end of each line we draw
        abAppend(ab, "\x1b[K", 3);

        // Print newline as last line after last row
        // since status bar is the final line drawn on screen
        abAppend(ab, "\r\n", 2);
    }
}

// Display the status bar with inverted colors on the screen
void editorDrawStatusBar(struct abuf *ab) {
    // <esc>[7m switches to inverted colors
    abAppend(ab, "\x1b[7m", 4);
    
    // Declare buffer for file and line status message 
    char status[80], rstatus[80];

    // Create a formatted file status message and store it in the buffer
    // The status message includes the filename and the number 
    // of lines in the file, formatted as "<filename> - <numrows> lines"
    // If the filename is NULL, use "[No Name]" instead
    // If the dirty flag is not 0 after the file is modified, then we show "(modified)"
    // The 'snprintf' function ensures that the status message doesn't exceed 
    // the size of the status buffer, truncating the filename to 20 characters if needed
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
        E.filename ? E.filename : "[No Name]", E.numrows, 
        E.dirty ? "(modified)" : "");

    // Create a formatted line status message and store it in the buffer
    // The status message includes the current line number and number of rows
    // Formatted at "<filetype> | <current line>/<numrows>"
    // Current line stored in cy and we add 1 since cy is 0-indexed
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

    // If the length of the status doesn't fit inside 
    // the width of the window, cut the string short for fit
    if (len > E.screencols) len = E.screencols;

    // Append the file status message
    abAppend(ab, status, len);
    
    // Draw blank white status bar of inverted space characters
    while (len < E.screencols) {
        // If the line status string is against the right edge of the screen
        // stop printing spaces and append the second status message
        // Otherwise, print a space
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    // <esc>[m switches back to normal formatting
    abAppend(ab, "\x1b[m", 3);

    // Print new line after the first status bar 
    // to allow space for the second status message
    abAppend(ab, "\r\n", 2);
}

// Displays the status message
void editorDrawMessageBar(struct abuf *ab) {
    // Clear the message bar with <esc>[K sequence
    abAppend(ab, "\x1b[K", 3);

    // Get the length of the status message
    int msglen = strlen(E.statusmsg);

    // Truncate the message length if it is bigger than 
    // the width of the screen
    if (msglen > E.screencols) msglen = E.screencols;

    // Display the status message only if the message
    // is less than 5 seconds old
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

// Clear screen and reposition cursor for rendering 
// editor UI after each keypress
void editorRefreshScreen() {
    editorScroll();

    // Initialize new abuf
    struct abuf ab = ABUF_INIT;

    // Hide cursor before refreshing the screen
    abAppend(&ab, "\x1b[?25l", 6);

    // Write 3 bytes out to terminal
    // Byte 1 is \x1b (escape char) and other 2 bytes 
    // are [H (arg for repositioning cursor at row 1 column 1)
    abAppend(&ab, "\x1b[H", 3);

    // Draw tilde row buffer
    editorDrawRows(&ab);

    // Draw the status bar on the second to last line of the screen
    editorDrawStatusBar(&ab);

    // Draw the status message on the last line of the screen
    editorDrawMessageBar(&ab);

    // Declare buffer to store formatted string
    char buf[32];

    // Specify exact position we want cursor 
    // to move to and store it in the buffer
    // Add 1 to x and y position to convert 0 index values to 
    // 1 index values that the terminal uses
    // NOTE: rx is being used since scrolling should take into account
    // the chars that are actually rendered and rendered position of the cursor
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);

    // Append formatted string to abuf
    abAppend(&ab, buf, strlen(buf));

    // Show cursor after refresh
    abAppend(&ab, "\x1b[?25h", 6);

    // Write buffer's contents to standard output
    write(STDOUT_FILENO, ab.b, ab.len);

    // Free memory used by the abuf
    abFree(&ab);
}

// Variadic function that stores the status message 
// in the global state and sets the message timestamp
void editorSetStatusMessage(const char *fmt, ...) {
    // Retrieve any additional args
    va_list ap;

    // Initialize the additional args after the fmt param
    va_start(ap, fmt);

    // Store resulting string to status message global buffer
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);

    // Cleanup additional args
    va_end(ap);

    // Set to the current time when the status message was stored
    E.statusmsg_time = time(NULL);
}

/*** input ***/

// Displays a prompt in the status bar and let
// the user input a line of text after the prompt
// If function is provided as an arg, then it will be called 
// after each keypress, passing the current search query 
// inputted by the user and the last key they pressed
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    // Buffer size
    size_t bufsize = 128;

    // Buffer to store user input
    char *buf = malloc(bufsize);

    // Initialize buffer as empty string
    size_t buflen = 0;
    buf[0] = '\0';

    // Infinite loop that repeatedly sets the status message,
    // refreshes the screen, and waits for a keypress to handle
    while (1) {
        // Set status message
        editorSetStatusMessage(prompt, buf);

        // Refresh screen
        editorRefreshScreen();

        // Get the keypress input
        int c = editorReadKey();
        
        // If the keypress is Delete, Ctrl-H, or Backspace, then delete the 
        // last char in the user's input by replacing it with a null byte
        // If keypress is Esc, cancel the input prompt
        // If keypress is Enter and the input is not empty,
        // then the status message is cleared and the user input is returned
        // Otherwise, when the user inputs a printable char, append it
        // to the buffer and if the buffer length is at the max, then double the 
        // the buffer size and allocate the memory before appending to the buffer
        // Note: we check if the input isn't a special key as specified in the editorKey
        // enum, so the inputs are in the range of a char (< 128)
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            // If the buffer is not empty, then overwrite last char in the buffer
            // as a null byte and decrement the buffer length
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            // Clear status message
            editorSetStatusMessage("");

            // If callback function is provided, then call 
            // it and pass the user input buffer and keypress 
            // input to the function as args
            if (callback) callback(buf, c);
            
            // Free buffer 
            free(buf);

            // Return null to abort the save operation
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }

            // Append to the buffer
            buf[buflen++] = c;

            // Append null byte at the end of the buffer
            buf[buflen] = '\0';
        }

        // If callback function is provided, then call 
        // it and pass the user input buffer and keypress 
        // input to the function as args
        if (callback) callback(buf, c);
    }
}

// Allow user to move cursor using arrow keys if within the bounds
void editorMoveCursor(int key) {
    // Since cursor y position is allowed to be one past the last
    // line of the file, check if the cursor is on an actual line
    // If it is, then row will point to the erow that the cursor is on
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            // Decrement x position if it isn't 0
            // Check if the cursor isn't on the first line, if it isn't,
            // then allow the cursor to move to the end of the previous line
            // if it is at the beginning of the current line
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            // Check if cursor x position is to the left
            // of the end of the line before we allow
            // cursor to move to the right
            // Check if the cursor is at the end of a line 
            // (not end of the file), if it is, then allow
            // the cursor go to the start of the next line
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            // Allow cursor to advance past the bottom of the screen
            // but not past the bottom of the file
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    // Since cursor y position is allowed to be one past the last
    // line of the file, check if the cursor is on an actual line
    // If it is, then row will point to the erow that the cursor is on
    // Setting row again since y position could point to a different line than before
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    // If row is null, then we consider it to be length 0
    // Otherwise, set length to the current size of the row
    int rowlen = row ? row->size : 0;

    // Set cursor x position to the end of the line
    // if it is to the right of the end of the line
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

// Map keypresses to editor operations
void editorProcessKeypress() {
    // To quit without saving, we will require
    // the user to Ctrl-Q 3 more times
    static int quit_times = CONFIRM_QUIT_TIMES;

    // Get returned keypress
    int c = editorReadKey();

    switch (c) {
        // Enter key inserts a new line
        case '\r':
            editorInsertNewline();
            break;

        // Exit program, clear screen, and reset cursor position
        // If quitting with unsaved changes, then the user will need to 
        // Ctrl-Q 3 more times to fully exit the editor
        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        // Save file to disk
        case CTRL_KEY('s'):
            editorSave();
            break;

        // TEMPORARY: Move cursor to left edge of screen
        case HOME_KEY:
            E.cx = 0;
            break;
        
        // Move the cursor to the end of the current line
        // If there's no current line then the cursor x position is 0
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;

        // Enables user to search within the file
        case CTRL_KEY('f'):
            editorFind();
            break;
        
        // Delete a character left of the cursor
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            // Pressing the right arrow and then backspace is
            // the same as pressing the delete key and deleting
            // the char to the right of the cursor
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        // Scroll up and down the entire page
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    // Position the cursor top of the screen
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    // Position cursor bottom of the screen
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                // Simulate an entire screen's worth of moving up or down keypresses
                int times = E.screenrows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        // Move cursor 
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        
        // Ignore the CTRL-L and Esc keypresses
        case CTRL_KEY('l'):
        case '\x1b':
            break;

        // Allow any keypress that isn't mapped to another editor function
        // to be inserted directly into the text being edited
        default:
            editorInsertChar(c);
            break;
    }

    // Reset the quit counter
    quit_times = CONFIRM_QUIT_TIMES;
}

/*** init ***/

// Initialize all fields in global struct
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL; // Will stay null if no file is opened
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;
    
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    
    // Decrement by 2 so editorDrawRows() doesn’t try to draw a line of 
    // text at the bottom of the screen and to allow space for the 
    // status message to prompt users for input
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    // Set initial status message to help message with key bindings
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
