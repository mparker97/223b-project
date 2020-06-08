SHELL := /bin/bash
CC := gcc
CFLAGS := -I.

MAINDIR := ..
MAINDIRHS := file.h sql.h common.h interval_tree.h range.h
MAINDIROS := main.o file.o interval_tree.o range.o
THISDIROS := sql.o
OS := $(addprefix $(MAINDIR)/, $(MAINDIROS)) $(THISDIROS)
HS := $(addprefix $(MAINDIR)/, $(MAINDIRHS))
#sed -n #p [file]
#	replace # with line number
LIBS := -lpthread

BYTEOFFSETS := bo
FSDIR := fs
UNPARSEDDIR := unparsed

.PHONY: all parse byte-offsets
all: 223b parse

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

parse: byte-offsets parse-files
#parse-files: $(UNPARSEDDIR)/*
#	rm -rf $(FSDIR)
#	mkdir $(FSDIR)
#	@for a in $(notdir $^); do \
#		echo $$a:; \
#		cat $(UNPARSEDDIR)/$$a | ./$(BYTEOFFSETS) > $(FSDIR)/$$a; \
#		echo; \
#	done
parse-files: $(shell find $(UNPARSEDDIR))
	rm -rf $(FSDIR)
	@for a in $^; do \
		b=$${a/$(UNPARSEDDIR)/$(FSDIR)}; \
		if [ -d $$a ]; then \
			mkdir $$b; \
		else \
			echo $$a:; \
			cat $$a | ./$(BYTEOFFSETS) > $$b; \
			echo; \
		fi; \
	done

byte-offsets: $(BYTEOFFSETS)
$(BYTEOFFSETS): %: %.c
	$(CC) -o $@ $<

.PHONY: clean
clean:
	rm -f ./*.o
	rm -f 223b
	rm -f $(BYTEOFFSETS)