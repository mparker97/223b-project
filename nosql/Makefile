CC = gcc
CFLAGS = -I.

MAINDIR = ..
MAINDIRHS = file.h sql.h common.h interval_tree.h range.h
MAINDIROS = main.o file.o interval_tree.o range.o
THISDIROS = sql.o
OS = $(addprefix $(MAINDIR)/, $(MAINDIROS)) $(THISDIROS)
HS = $(addprefix $(MAINDIR)/, $(MAINDIRHS))

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