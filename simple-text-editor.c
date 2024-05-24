#include <ctype.h> // Access iscntrl()
#include <stdio.h> // Access printf()
#include <stdlib.h> // Access atexit()
#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, TCSAFLUSH, ICANON, ISIG, IXON, IEXTEN, ICRNL
#include <unistd.h> // Access read(), STDIN_FILENO

struct termios orig_termios; // Store original terminal attributes

// Restore original terminal attributes after enabling raw mode and exiting program
void disableRawMode() {
    // Discard any unread input before restoring terminal attributes
    tcsetattr(STDERR_FILENO, TCSAFLUSH, &orig_termios);
}

// Disable the echoing of input characters in the terminal (raw mode)
// Set terminal's attributes by reading current attributes,
// modifying it, and writing new terminal attributes back out
void enableRawMode() {
    // Store original terminal attributes into global struct
    tcgetattr(STDIN_FILENO, &orig_termios);

    // disableRawMode called when program exits
    atexit(disableRawMode);   

    // Declare new struct which will record all the I/O attributes of a terminal
    struct termios raw = orig_termios;

    // Turn off translating carriage returns to newlines
    // Turn off XON/XOFF to avoid resuming/pausing data transmission (disable Ctrl-Q/S)
    raw.c_iflag &= ~(ICRNL | IXON);

    // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary
    // Bitwise-AND inverted ECHO bits with the flag's field, which forces the fourth bit to be 0 
    // and causes every other bit to retain its current value.
    // Turn off canonical mode, read input byte-by-byte instead of line-by-line
    // Turn off IEXTEN to disable Ctrl-V (or O) to avoid stopping inputs
    // Turn off sending SIGINT/SIGSTP to avoid terminating/suspending (disable Ctrl-C/Z)
    // NOTE: ICANON/IEXTEN are local flags in c_lflag field
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Read current terminal attributes into struct
    tcgetattr(STDERR_FILENO, &raw);

    // Set the modified terminal attributes for standard input
    // TCSAFLUSH for applying changes after flushing input buffer to discard any unread input
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    
    // Keep reading 1 byte from standard input into variable c until there are no more bytes to read
    // read() returns number of bytes read, will return 0 when it reaches the end of a file
    // Program will exit when q keypress is read from the user
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        // Check if character is a control/printable character
        if (iscntrl(c)) {
            // Format byte as decimal
            printf("%d\n", c);
        } else {
            // Write byte directly as character
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}
