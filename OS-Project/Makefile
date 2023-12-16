CFLAGS = -std=c89 -pedantic
EXECUTE = master

all: clean master users nodes

clean:
	rm -f *.o master users nodes
master:
	gcc $(CFLAGS) master.c -o master
users:
	gcc $(CFLAGS) users.c -o users
nodes:
	gcc $(CFLAGS) nodes.c -o nodes

run:
	./$(EXECUTE)