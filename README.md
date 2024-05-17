This is a minimal text editor built in C based on [kilo](http://antirez.com/news/108). It implements basic features you would expect in a text editor including syntax highlighting and search. 

## Check C Compiler and `make`
- Run `cc --version` to check if you have a C compiler installed.
- Run `make -v` to check if `make` is installed.
- If you're missing any of these, search how to install a C compiler based on your OS (Windows, macOS, Linux distribution).
  
## Compiling and Running
- You can choose to compile using `cc simple-text-editor.c -o simple-text-editor` in your shell to produce the executable and run using `./simple-text-editor` afterwards.
- There is a `Makefile` included, so you can call `make` in your shell to compile the program (you may see some warnings, but it should be fine) and run using `./simple-text-editor`.