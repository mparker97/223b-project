#ifndef HELP_H
#define HELP_H
#include <stdio.h>
#include "common.h"

#define USAGE_MESSAGE "USAGE: %s MODE [ARGS] [(-e)|(-E) EXE_PATH [EXE_OPTIONS]]\n"
#define HELP_MESSAGE "\n\
Arguments are shown using regex.\n\
\
	MODE: Select one of the following:\n\
		-h\n\
			Help: Display this text.\n\
		-r RANGE_NAME (-f)? (FILE_PATH)+\n\
			Read: Open specified range for reading. No changes will be persisted.\n\n\
				RANGE_NAME: The name of the range to open.\n\
				FILE_PATH: The files within that range to open.\n\
		-w RANGE NAME (-f)? (FILE_PATH)+\n\
			Write: Open specified range for writing. Changes within the range will be persisted.\n\
				RANGE_NAME: The name of the range to open.\n\
				FILE_PATH: The files within that range to open.\n\
		-g RANGE_NAME (-f (FILE_PATH)+ -r (INTERVAL)+)+\n\
			New Range: Create a new named range.\n\
				RANGE_NAME: The name of the range to create.\n\
				Adjacent (-f, -r) flags are treated as a pair as follows:\n\
					The -f is followed by a set of file paths to include in the named range.\n\
					The subsequent -r is followed by intervals to be applied\n\
					to only the files given after this pair's -f flag.\n\
				INTERVAL should be written as \"A,B\" (without quotes).\n\
					This indicates an interval from byte A up to but not including byte B.\n\
					To start at the beginning of a file, withhold A (e.g. \",32\").\n\
					To go until the end of a file. withhold B (e.g. \"32,\".\n\
		-p (((-g)|(-r) (RANGE_NAME)+)* (-f (FILE_PATH)+)*)*\n\
			Print: Display named ranges.\n\
				The -g or -r flag gives a set of ranges to print.\n\
					All files and intervals of each range will be printed.\n\
				The -f flag gives a set of files to print.\n\
					All intervals in each file along with their affiliated range names will be printed.\n\
				If no argument is specified, all ranges are printed.\n\
\
	THE EXECUTABLE:\n\
		An executable must be supplied for the -r and -w modes.\n\
		Once the -e or -E flag is encountered, all processing of ARGS stops.\n\
		EXE_PATH is a path to the executable (text editor) to use to open the file(s).\n\
		EXE_OPTIONS are options for this executable.\n\
\
			For -e, each file will be opened in a separate process. Unsuccessful opens will be notified via stderr.\n\
			Changes to a file are persisted once its process terminates.\n\
\
			For -E, the executable is run in one process with all files passed in as arguments.\n\
			This is essential for editors that are \"embedded\" in the terminal, such as vim, as it allows for\n\
			these programs to run & lock ranges that span multiple files.\n\
			For example, to edit files \"foo\", \"bar\", and \"xyzzy\" of a common range in vim, run with the flags:\n\
\
				-w foo bar xyzzy -E vim -p\n\
\
			Vim's -p option is used to display multiple files, each in its own tab.\n\n\
"

void print_usage(char* s){
	fprintf(stdout, USAGE_MESSAGE, s);
}

void print_help(char* s){
	print_usage(s);
	fprintf(stdout, HELP_MESSAGE);
	err(0);
}

#endif
