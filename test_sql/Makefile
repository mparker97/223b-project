CC = gcc
CFLAGS = -I.
HS = options.h sql.h common.h list.h interval_tree.h range.h help.h zkclient.h
OS = main.o sql.o interval_tree.o range.o 
LIBS = -lmysqlclient -lpthread


.PHONY: all

223b: $(OS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OS): %.o: %.c $(HS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)


.PHONY: zookeeper
zookeeper: $(ZKOS)

.PHONY: clean
clean:
	rm -f ./*.o
	rm -f 223b
