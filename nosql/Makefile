SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O -Wall -Wno-unused-function -Wno-unused-result

MAINDIR := ..
MAINDIRHS := file.h sql.h common.h interval_tree.h range.h help.h
MAINDIROS := main.o file.o interval_tree.o range.o
THISDIROS := sql.o
THISDIRHS := zkclient.h
OS := $(addprefix $(MAINDIR)/, $(MAINDIROS)) $(THISDIROS)
HS := $(addprefix $(MAINDIR)/, $(MAINDIRHS)) $(THISDIRHS)

LIBS := -lpthread

BYTEOFFSETS := bo
RANGEFILE := ranges
FSDIR := fs
UNPARSEDDIR := unparsed

CMD = -h
RANGEID = 1

EXEC := 223b

.PHONY: all exec_helper returnhs test parse parse-files byte-offsets cleanfs clean
all: $(EXEC) parse

$(EXEC): exec_helper returnhs
exec_helper: $(OS)
	$(CC) -o $(EXEC) $^ $(CFLAGS) $(LIBS)
$(OS): %.o: %.c $(HS)
	@for a in $(THISDIRHS); do \
		echo moving $$a to main directory; \
		mv $(MAINDIR)/$$a $(MAINDIR)/.$$a 2>/dev/null; \
		cp $$a $(MAINDIR)/$$a; \
	done
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS) || $(MAKE) returnhs

returnhs: $(THISDIRHS)
	@for a in $(THISDIRHS); do \
		if [ -a $(MAINDIR)/.$$a ]; then \
			echo returning $(MAINDIR)/$$a to previous state; \
			mv $(MAINDIR)/.$$a $(MAINDIR)/$$a 2>/dev/null; true; \
		fi; \
	done

# For example: make test CMD=-g RANGEID=2
test: $(EXEC)
	@sed -n $(RANGEID)p $(RANGEFILE) | xargs ./$(EXEC) $(CMD)

parse: byte-offsets parse-files

parse-files: $(shell find $(UNPARSEDDIR)/*)
	rm -rf $(FSDIR)/*
	rm -f $(RANGEFILE)
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
	rm -f $(RANGEFILE)
clean:
	rm -f ./*.o
	rm -f $(EXEC) $(BYTEOFFSETS)