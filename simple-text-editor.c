/*** includes ***/

#include <ctype.h> // Access iscntrl()
#include <errno.h> // Access errno, EAGAIN
#include <stdio.h> // Access printf(), perror(), sscanf()
#include <stdlib.h> // Access atexit(), exit(), realloc(), free()
#include <string.h> // Acess memcpy()
#include <sys/ioctl.h> // Access ioctl(), TIOCGWINSZ, struct winsize
#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <unistd.h> // Access read(), STDIN_FILENO, write()

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // CTRL keypress macro

/*** data ***/

// Global struct to contain editor state
struct editorConfig {
    // Number of current rows 
    int screenrows;

    // Number of current columns
    int screencols; 

    // Store original terminal attributes
    struct termios orig_termios; 
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
char editorReadKey() {
    int nread;
    char c;

    // Keep reading 1 byte from standard input into variable c until there are no more bytes to read
    // read() returns number of bytes read, will return 0 when it reaches the end of a file
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    return c;
}

// 
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

// Draw row of tildes (similar to vim)
void editorDrawRows(struct abuf *ab) {
    int y;

    // Draw tildes based on current number of rows in terminal
    for (y = 0; y < E.screenrows; y++) {
        // Output tilde in the row
        abAppend(ab, "~", 1);

        // Print newline as last line
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

// Clear screen and reposition cursor for rendering 
// editor UI after each keypress
void editorRefreshScreen() {
    // Initialize new abuf
    struct abuf ab = ABUF_INIT;

    // Write 4 bytes out to terminal
    // Byte 1 is \x1b (escape char) and other 3 bytes 
    // are [2J (arg for clearing entire screen)
    abAppend(&ab, "\x1b[2J", 4);

    // Write 3 bytes out to terminal
    // Byte 1 is \x1b (escape char) and other 2 bytes 
    // are [H (arg for repositioning cursor at row 1 column 1)
    abAppend(&ab, "\x1b[H", 3);

    // Draw tilde row buffer
    editorDrawRows(&ab);

    // Reposition cursor
    abAppend(&ab, "\x1b[H", 3);

    // Write buffer's contents to standard output
    write(STDOUT_FILENO, ab.b, ab.len);

    // Free memory used by the abuf
    abFree(&ab);
}

/*** input ***/

// Map keypresses to editor operations
void editorProcessKeypress() {
    // Get returned keypress
    char c = editorReadKey();

    switch (c) {
        // Exit program, clear screen, and reset cursor position
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/

// Initialize all fields in global struct
void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
