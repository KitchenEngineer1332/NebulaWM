CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lX11
XFT_CFLAGS = $(shell pkg-config --cflags xft)
XFT_LIBS = $(shell pkg-config --libs xft) -lX11
IMLIB2_CFLAGS = $(shell pkg-config --cflags imlib2)
IMLIB2_LIBS = $(shell pkg-config --libs imlib2)

SRC = main.c config.c
OBJ = $(SRC:.c=.o)
EXEC = nebulawm
LAUNCHER = nebula-launcher
POWERMENU = nebula-powermenu
LOCKSCREEN = nebula-lockscreen

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONSDIR ?= /usr/share/xsessions

all: $(EXEC) $(LAUNCHER) $(POWERMENU) $(LOCKSCREEN)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $(EXEC)

$(LAUNCHER): launcher.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) launcher.c $(XFT_LIBS) -o $(LAUNCHER)

$(POWERMENU): powermenu.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) powermenu.c $(XFT_LIBS) -o $(POWERMENU)

$(LOCKSCREEN): lockscreen.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) $(IMLIB2_CFLAGS) lockscreen.c $(XFT_LIBS) $(IMLIB2_LIBS) -lpam -o $(LOCKSCREEN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC) $(LAUNCHER) $(POWERMENU) $(LOCKSCREEN)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)
	install -m 755 $(LAUNCHER) $(DESTDIR)$(BINDIR)/$(LAUNCHER)
	install -m 755 $(POWERMENU) $(DESTDIR)$(BINDIR)/$(POWERMENU)
	install -m 755 $(LOCKSCREEN) $(DESTDIR)$(BINDIR)/$(LOCKSCREEN)
	install -d $(DESTDIR)$(XSESSIONSDIR)
	install -m 644 nebulawm.desktop $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop
	@if [ -n "$$SUDO_USER" ]; then \
		USER_HOME=$$(getent passwd $$SUDO_USER | cut -d: -f6); \
		install -d -m 755 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) $$USER_HOME/.config; \
		install -d -m 755 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) $$USER_HOME/.config/Nebula; \
		if [ ! -f $$USER_HOME/.config/Nebula/lockscreen.conf ]; then \
			install -m 644 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) lockscreen.conf $$USER_HOME/.config/Nebula/lockscreen.conf; \
		fi; \
	else \
		install -d -m 755 ~/.config/Nebula; \
		if [ ! -f ~/.config/Nebula/lockscreen.conf ]; then \
			install -m 644 lockscreen.conf ~/.config/Nebula/lockscreen.conf; \
		fi; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)
	rm -f $(DESTDIR)$(BINDIR)/$(LAUNCHER)
	rm -f $(DESTDIR)$(BINDIR)/$(POWERMENU)
	rm -f $(DESTDIR)$(BINDIR)/$(LOCKSCREEN)
	rm -f $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop
