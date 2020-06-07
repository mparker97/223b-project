CC = gcc
CFLAGS = -I.
HS = file.h options.h sql.h common.h list.h interval_tree.h range.h help.h
OS = main.o file.o sql.o interval_tree.o range.o
LIBS = -lpthread

.PHONY: all
all: 223b

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

.PHONY: clean
clean:
	rm -f ./*.o
	rm -f 223b