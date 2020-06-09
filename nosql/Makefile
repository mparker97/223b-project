SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O -Wall -Wno-unused-function -Wno-unused-result

MAINDIR := ..
MAINDIRHS := file.h sql.h common.h interval_tree.h range.h
MAINDIROS := main.o file.o interval_tree.o range.o
THISDIROS := sql.o
OS := $(addprefix $(MAINDIR)/, $(MAINDIROS)) $(THISDIROS)
HS := $(addprefix $(MAINDIR)/, $(MAINDIRHS))

LIBS := -lpthread

BYTEOFFSETS := bo
RANGEFILE := ranges
FSDIR := fs
UNPARSEDDIR := unparsed

CMD = -h
RANGEID = 1

EXEC := 223b

.PHONY: all test parse parse-files byte-offsets cleanfs clean
all: $(EXEC) parse

$(EXEC): $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

test: $(EXEC)
	# For example: make test CMD=-g RANGEID=2
	sed -n $(RANGEID)p $(RANGEFILE) | xargs ./$(EXEC) $(CMD)

parse: byte-offsets parse-files

parse-files: $(shell find $(UNPARSEDDIR)/*)
	rm -rf $(FSDIR)/*
	rm -rf $(RANGEFILE)
	@for a in $^; do \
		b=$${a/$(UNPARSEDDIR)/$(FSDIR)}; \
		if [ -d $$a ]; then \
			mkdir $$b; \
		else \
			echo Parsing $$a; \
			cat $$a | ./$(BYTEOFFSETS) $(RANGEFILE) $$b > $$b; \
		fi; \
	done

byte-offsets: $(BYTEOFFSETS)
$(BYTEOFFSETS): %: %.c
	$(CC) -o $@ $< $(CFLAGS)

cleanfs: clean
	rm -rf $(FSDIR)/*
	rm -rf $(RANGEFILE)
clean:
	rm -f ./*.o
	rm -f $(EXEC) $(BYTEOFFSETS)