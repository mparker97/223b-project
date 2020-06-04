CC = gcc
CFLAGS = -I.
HS = common.h list.h range.h interval_tree.h sql.h help.h
OS = main.o sql.o
LIBS = -lmysql -lpthread

%.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f ./*.o