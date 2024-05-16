# Want to build simple-text-editor using simple-text-editor.c
# $(CC): a variable in which make uses cc by default
# -Wall: "All Warnings", compiler gives warnings of code
# -Wextra: Enables extra warnings
# -pedantic: More warnings to adhere to language standard
# -std=c99: Specify version of the C language standard (C99)
# C99 allows variables to be declared anywhere within a function rather than the top of a function
simple-text-editor: simple-text-editor.c
	$(CC) simple-text-editor.c -o simple-text-editor -Wall -Wextra -pedantic -std=c99