#ifndef HELP_H
#define HELP_H
#include <stdio.h>

const char* HELP_MESSAGE;
const char* USAGE_MESSAGE;

void print_usage(char* s){
	fprintf(stdout, USAGE_MESSAGE, s);
}

void print_help(char* s){
	print_usage(s);
	fprintf(stdout, HELP_MESSAGE);
	err(0);
}

USAGE_MESSAGE = "USAGE: %s MODE [OPTIONS] [-e EXE_PATH [EXE_OPTIONS]]\n";

HELP_MESSAGE = "\n\
	MODE: Select one of the following:\n\
		-h  Help: Display this text.\n\
		-r  Read: Open a specified range for reading.\n\
			No changes will be persisted.\n\
		-w  Write: Open specified range for writing.\n\
			Changes within the range will be persisted.\n\
		-n  Insert: Open a specified set of locations for inserting.\n\
			Changes at these locations will be persisted.\n\
		-g  New Range: Create a new named range.\n\
		-p  Print: Display named ranges.\n\
	OPTIONS:\n\
		-f  Files: \n\
		-r  Regions: \n\
		-s  Read from standard input.\n\
			File and Region data will be read from standard input using the same format as above.\n\
	THE EXECUTABLE:\n\
		Once the -e flag is encountered, all processing of OPTIONS stops.\n\
		EXE_PATH is a path to the executable (text editor) to use to open the files.\n\
		EXE_OPTIONS are options for this executable.\n\
"; // TODO

#endif