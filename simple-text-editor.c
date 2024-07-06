/*** includes ***/

#define _DEFAULT_SOURCE // Enable default set of features provided by glibc
#define _BSD_SOURCE // Enable BSD extensions in glibc
#define _GNU_SOURCE // Enables all GNU extensions

#include <ctype.h> // Access iscntrl()
#include <errno.h> // Access errno, EAGAIN
#include <stdio.h> // Access printf(), perror(), sscanf(), snprintf(), FILE, fopen(), getline()
#include <stdlib.h> // Access atexit(), exit(), realloc(), free(), malloc()
#include <string.h> // Acess memcpy(), strlen()
#include <sys/ioctl.h> // Access ioctl(), TIOCGWINSZ, struct winsize
#include <sys/types.h> // Access ssize_t
#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <unistd.h> // Access read(), STDIN_FILENO, write()

/*** defines ***/

#define SIMPLE_TEXT_EDITOR_VERSION "0.0.1" // Version number for welcome message display
#define TAB_STOP_LENGTH 8 // The length of a tab stop
#define CTRL_KEY(k) ((k) & 0x1f) // CTRL keypress macro

// Keys that move the cursor or page in the editor
// Represent keys with large integer values out of 
// range for char to avoid conflicting with ordinary keypresses
enum editorKey {
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

/*** data ***/

// Data type for storing a row of text in the editor
typedef struct erow {
    // Length
    int size;

    // Contains size of the contents of render
    int rsize;

    // Allocated char data
    char *chars;

    // Contains the actual chars to draw on the screen for the row
    // Needed to render nonprintable control chars i.e. tabs
    char *render;
} erow;

// Global struct to contain editor state
struct editorConfig {
    // Cursor's x and y position
    int cx, cy;

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
};

struct editorConfig E;

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

/*** row operations ***/

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
}

// Appends line read from file to as new entry to erow struct array
void editorAppendRow(char *s, size_t len) {
    // Reallocate memory to resize array to append new line
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // Index of the new row we want to initialize 
    int at = E.numrows;

    // Set row size to length of the line
    E.row[at].size = len;

    // Allocate memory for the line 
    E.row[at].chars = malloc(len + 1);

    // Store line to chars field which points to the allocated memory
    memcpy(E.row[at].chars, s, len);

    // Terminate string with null char
    E.row[at].chars[len] = '\0';

    // Initialize render
    E.row[at].rsize = 0;
    E.row[at].render = NULL;

    // Update render string
    editorUpdateRow(&E.row[at]);

    // Update number of rows
    E.numrows++;
}

/*** file i/o ***/

// Opens and reads a files from disk
void editorOpen(char *filename) {
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
        editorAppendRow(line, linelen);
    }

    // Free the memory used by the buffer 
    free(line);

    // Close the file since we're done reading
    fclose(fp);
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
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }

    // Check if the cursor is horizontally past the visible window
    // If it is, scroll right to the cursor
    if (E.cx >= E.coloff + E.screencols) {
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

            // Write out the string (the render string specifically)
            // To display each row at the column offset,
            // E.coloff is used an index for the chars of each erow displayed
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        // Clear each line as they're redrawn instead 
        // of clearing entire screen before each refresh
        // Put <esc>[K sequence at the end of each line we draw
        abAppend(ab, "\x1b[K", 3);

        // Print newline as last line
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
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

    // Declare buffer to store formatted string
    char buf[32];

    // Specify exact position we want cursor 
    // to move to and store it in the buffer
    // Add 1 to x and y position to convert 0 index values to 
    // 1 index values that the terminal uses
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);

    // Append formatted string to abuf
    abAppend(&ab, buf, strlen(buf));

    // Show cursor after refresh
    abAppend(&ab, "\x1b[?25h", 6);

    // Write buffer's contents to standard output
    write(STDOUT_FILENO, ab.b, ab.len);

    // Free memory used by the abuf
    abFree(&ab);
}

/*** input ***/

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
    // Get returned keypress
    int c = editorReadKey();

    switch (c) {
        // Exit program, clear screen, and reset cursor position
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        // TEMPORARY: Move cursor to left or right edges of screen
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        // TEMPORARY: Move cursor to top and bottom of screen
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }

        // Move cursor 
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

// Initialize all fields in global struct
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
