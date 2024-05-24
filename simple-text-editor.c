/*** includes ***/

#include <ctype.h> // Access iscntrl()
#include <errno.h> // Access errno, EAGAIN
#include <stdio.h> // Access printf(), perror()
#include <stdlib.h> // Access atexit(), exit()
#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL, OPOST, BRKINT, INPCK, ISTRIP, CS8, VMIN, VTIME
#include <unistd.h> // Access read(), STDIN_FILENO, write()

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // CTRL keypress macro

/*** data ***/

// Global struct to contain editor state
struct editorConfig {
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

/*** output ***/

// Draw column of tildes (similar to vim)
void editorDrawRows() {
    int y;

    // Draw tildes in each row
    // TODO: Don't currently know size of terminal, defaulting to 24 rows
    for (y = 0; y < 24; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

// Clear screen and reposition cursor for rendering 
// editor UI after each keypress
void editorRefreshScreen() {
    // Write 4 bytes out to terminal
    // Byte 1 is \x1b (escape char) and other 3 bytes 
    // are [2J (arg for clearing entire screen)
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // Write 3 bytes out to terminal
    // Byte 1 is \x1b (escape char) and other 2 bytes 
    // are [H (arg for repositioning cursor at row 1 column 1)
    write(STDOUT_FILENO, "\x1b[H", 3);

    // Draw tilde row buffer
    editorDrawRows();

    // Reposition cursor
    write(STDOUT_FILENO, "\x1b[H", 3);
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

int main() {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
