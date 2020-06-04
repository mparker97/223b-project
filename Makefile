CC = gcc
CFLAGS = -I.
HS = sql.h common.h list.h interval_tree.h range.h  zkclient.h help.h
OS = main.o sql.o interval_tree.o range.o zkclient.o tests.o
LIBS = -lmysql -lpthread

%.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f ./*.o