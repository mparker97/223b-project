SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O -Wall -Wno-unused-function -Wno-unused-result
HS := file.h options.h sql.h common.h list.h interval_tree.h range.h help.h zkclient.h pcq.h
OS := main.o file.o sql.o interval_tree.o range.o tests.o
LIBS := -lmysqlclient -lpthread -lzookeeper_mt

ZKHS := sql.h interval_tree.h range.h zkclient.h
ZKOS := zkclient.o
ZKLIBS := -lpthread -lzookeeper-mt

EXEC := 223b

.PHONY: all zookeeper clean
all: $(EXEC)

$(EXEC): $(OS) $(ZKOS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

$(ZKOS): %.o: %.c $(ZKHS)
	$(CC) -c -o $@ $< $(CFLAGS) $(ZKLIBS)

zookeeper: $(ZKOS)

clean:
	rm -f ./*.o
	rm -f $(EXEC)