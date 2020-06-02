#ifndef HELP_H
#define HELP_H
#include <stdio.h>
#include "common.h"

#define USAGE_MESSAGE "USAGE: %s MODE [ARGS] [-e EXE_PATH [EXE_OPTIONS]]\n"
#define HELP_MESSAGE "\n\
MODE: Select one of the following:\n\
	-h\n\
		Help: Display this text.\n\
	-r RANGE_NAME [-f] FILE_PATH [...]\n\
		Read: Open specified range for reading. No changes will be persisted.\n\n\
			RANGE_NAME: The name of the range to open.\n\
			FILE_PATH [...]: The files within that range to open.\n\
	-w RANGE NAME [-f] FILE_PATH [...]\n\
		Write: Open specified range for writing. Changes within the range will be persisted.\n\
			RANGE_NAME: The name of the range to open.\n\
			FILE_PATH [...]: The files within that range to open.\n\
	-n RANGE_NAME -f FILE_PATH_0 OFFSET [...] -f FILE_PATH_1 OFFSET [...] [...]\n\
		Insert: Open specified set of locations for inserting. Changes at these locations will be persisted.\n\
			RANGE_NAME: The name of the range to open.\n\
			Each -f is followed by a file path of the range and an arbitrary\n\
				number of offsets within that file to insert.\n\
	-g RANGE_NAME -f FILE_PATH [...] -r INTERVAL [...] [...]\n\
		New Range: Create a new named range.\n\
			RANGE_NAME: The name of the range to create.\n\
			Adjacent (-f, -r) flags are treated as a pair as follows:\n\
				The -f is followed by a set of file paths to include in the named range.\n\
				The subsequent -r is followed by intervals to be applied\n\
				to only the files given after this pair's -f flag.\n\
			INTERVAL should be written as \"A,B\" (without quotes).\n\
				This indicates an interval from byte A up to but not including byte B.\n\
				To start at the beginning of a file, withhold A (e.g. ",32").\n\
				To go until the end of a file. withhold B (e.g. "32,".\n\
	-p [-g|-r RANGE_NAME [...]] [-f FILE_PATH [...]]\n\
		Print: Display named ranges.\n\
			The -g or -r flag gives a set of ranges to print.\n\
				All files and intervals of each range will be printed.\n\
			The -f flag gives a set of files to print.\n\
				All intervals in each file along with their affiliated range names will be printed.\n\
THE EXECUTABLE:\n\
	Once the -e flag is encountered, all processing of ARGS stops.\n\
	EXE_PATH is a path to the executable (text editor) to use to open the file(s).\n\
	EXE_OPTIONS are options for this executable.\n\
	Each file will be opened in a separate process. Unsuccessful opens will be notified via stderr.\n\
	Changes to a file are persisted once its process terminates.\
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