BALBOAD_SRCS := balboad.c novena/eim.c
BALBOAD_OBJS := $(BALBOAD_SRCS:.c=.o)

CFLAGS := -O2 -Wall -I.

INSTALLDIR := /opt/balboa

all: balboad

balboad: $(BALBOAD_OBJS)
	$(CC) $(CFLAGS) -o balboad $(BALBOAD_OBJS)

install: balboad
	mkdir -p $(INSTALLDIR)/bin
	cp -f balboad $(INSTALLDIR)/bin/balboad

clean:
	rm -f balboad *.o novena/*.o
