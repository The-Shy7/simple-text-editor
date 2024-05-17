#include <termios.h> // Access struct termios, tcgetattr(), tcsetattr(), ECHO, and TCSAFLUSH
#include <unistd.h> // Access read() and STDIN_FILENO

void disableRawMode() {
    // TODO: Need to restore original terminal attributes after enabling raw mode and exiting program
}

// Disable the echoing of input characters in the terminal (raw mode )
// Set terminal's attributes by reading current attributes,
// modifying it, and writing new terminal attributes back out
void enableRawMode() {
    // Declare struct, will record all the I/O attributes of a terminal
    struct termios raw;

    // Read current terminal attributes into struct
    tcgetattr(STDERR_FILENO, &raw);

    // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary
    // Bitwise-AND inverted ECHO bits with the flag's field, which forces the fourth bit to be 0 
    // and causes every other bit to retain its current value.
    raw.c_lflag &= ~(ECHO);

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
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');

    return 0;
}