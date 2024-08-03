CC = gcc
CFLAGS = -Wall -Wextra

all: diskinfo disklist diskget diskput

diskinfo: diskinfo.c
	$(CC) $(CFLAGS) -o diskinfo diskinfo.c

disklist: disklist.c
	$(CC) $(CFLAGS) -o disklist disklist.c

diskget: diskget.c
	$(CC) $(CFLAGS) -o diskget diskget.c

diskput: diskput.c
	$(CC) $(CFLAGS) -o diskput diskput.c

clean:
	rm -f diskinfo disklist diskget diskput
