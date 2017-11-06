TARGETS= Bbuffer

CC_C = gcc

CFLAGS = -std=c99 -Wall -g -pthread -lpthread

all:clean $(TARGETS)

$(TARGETS):
	$(CC_C) $(CFLAGS) $@.c -o $@

clean:
	rm -f $(TARGETS)
