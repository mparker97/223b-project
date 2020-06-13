SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O -Wall -Wno-unused-function -Wno-unused-result

MAINDIR := ..
MAINDIRHS := sql_connection.h file.h sql.h common.h interval_tree.h range.h help.h
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
EFLAG = -e

EXEC := yacet
TEXTEXEC := cat

.PHONY: all exec_helper sendhs returnhs test parse parse-files byte-offsets cleanall cleanfs clean

$(EXEC): sendhs exec_helper returnhs

all: $(EXEC) parse
exec_helper: $(OS)
	$(CC) -o $(EXEC) $^ $(CFLAGS) $(LIBS)
$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS) || ($(MAKE) returnhs && false)

sendhs: $(THISDIRHS)
	@for a in $(THISDIRHS); do \
		echo moving $$a to main directory; \
		mv $(MAINDIR)/$$a $(MAINDIR)/.$$a 2>/dev/null; \
		cp $$a $(MAINDIR)/$$a; \
	done
returnhs: $(THISDIRHS)
	@for a in $(THISDIRHS); do \
		if [ -a $(MAINDIR)/.$$a ]; then \
			echo returning $(MAINDIR)/$$a to previous state; \
			mv $(MAINDIR)/.$$a $(MAINDIR)/$$a 2>/dev/null; true; \
		fi; \
	done

# For example: make test CMD=-g RANGEID=2
test: $(EXEC)
	$(eval a=$(shell sed -n $(RANGEID)p $(RANGEFILE)))
	$(eval TEXTEXEC=$(shell which $(TEXTEXEC)))
	./$(EXEC) $(CMD) $a $(EFLAG) $(TEXTEXEC)

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

cleanall: cleanfs
	$(MAKE) clean -C $(MAINDIR)
cleanfs: clean
	rm -rf $(FSDIR)/*
	rm -f $(RANGEFILE)
clean:
	$(MAKE) clean -C $(MAINDIR)
	rm -f ./*.o
	rm -f $(EXEC) $(BYTEOFFSETS)
	rm -f ./*.swp
