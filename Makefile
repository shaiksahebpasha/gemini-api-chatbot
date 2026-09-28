CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
LDLIBS ?= -lcurl

all: eris

eris: eris.c
	$(CC) $(CFLAGS) eris.c -o eris $(LDLIBS)

clean:
	rm -f eris

.PHONY: all clean
