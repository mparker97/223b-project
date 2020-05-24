CC = gcc
CFLAGS = -I.
HS = common.h range.h interval_tree.h sql.h
OS = main.o sql.o
LIBS = -mysql

%.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f ./*.o