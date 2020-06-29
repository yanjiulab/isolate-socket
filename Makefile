SERV=isolate_server
CLI=isolate_client
CFLAGS=-I

.PHONY: all clean

src = $(wildcard *.c)
obj = $(patsubst %.c, %.o, ${src})
target = $(SERV) $(CLI) 

%.o: %.c
	$(CC) -c -o $@ $<

all: $(target)

isolate_server: $(SERV).o netns.o
	$(CC) -o $(SERV) $(SERV).o netns.o

isolate_client: $(CLI).o netns.o
	$(CC) -o $(CLI) $(CLI).o netns.o

clean:
	rm -f $(obj) $(target)
