CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lX11

SRC = main.c config.c
OBJ = $(SRC:.c=.o)
EXEC = nebulawm

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONSDIR ?= /usr/share/xsessions

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $(EXEC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)
	install -d $(DESTDIR)$(XSESSIONSDIR)
	install -m 644 nebulawm.desktop $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)
	rm -f $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop
