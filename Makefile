SHELL := /bin/bash
CC := gcc
CFLAGS := -I. -g -O -Wall -Wno-unused-function -Wno-unused-result
HS := file.h options.h sql.h common.h list.h interval_tree.h range.h help.h zkclient.h
OS := main.o file.o sql.o interval_tree.o range.o tests.o
LIBS := -lmysqlclient -lpthread -lzookeeper_mt

ZKHS := sql.h interval_tree.h range.h zkclient.h
ZKOS := zkclient.o
ZKLIBS := -lpthread -lzookeeper-mt

.PHONY: all
all: 223b zookeeper

223b: $(OS) $(ZKOS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

$(ZKOS): %.o: %.c $(ZKHS)
	$(CC) -c -o $@ $< $(CFLAGS) $(ZKLIBS)

.PHONY: zookeeper
zookeeper: $(ZKOS)

.PHONY: clean
clean:
	rm -f ./*.o
	rm -f 223b