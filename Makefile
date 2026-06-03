CC = gcc
CFLAGS = -O3 -march=native -flto -Wall -Wextra
LDFLAGS = -Wl,-O1,--sort-common,--as-needed -s
LIBS = -lX11 -lXinerama -lm $(IMLIB2_LIBS)
XFT_CFLAGS = $(shell pkg-config --cflags xft) $(IMLIB2_CFLAGS)
XFT_LIBS = $(shell pkg-config --libs xft) -lX11 -lXinerama -lm $(IMLIB2_LIBS)
IMLIB2_CFLAGS = $(shell pkg-config --cflags imlib2)
IMLIB2_LIBS = $(shell pkg-config --libs imlib2)

SRC = main.c config.c theme.c
OBJ = $(SRC:.c=.o)
EXEC = nebulawm
LAUNCHER = nebula-launcher
POWERMENU = nebula-powermenu
LOCKSCREEN = nebula-lockscreen
BAR = nebula-bar
STARLIGHT = starlight
COMPOSITOR = nebula-compositor
COMP_LIBS = -lXcomposite -lXdamage -lXfixes -lXrender -lXext -lGL

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONSDIR ?= /usr/share/xsessions

all: $(EXEC) $(LAUNCHER) $(POWERMENU) $(LOCKSCREEN) $(BAR) $(STARLIGHT) $(COMPOSITOR)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) $(LDFLAGS) $(LIBS) $(XFT_LIBS) -o $(EXEC)

$(LAUNCHER): launcher.c theme.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(XFT_CFLAGS) launcher.c theme.o $(XFT_LIBS) -o $(LAUNCHER)

$(POWERMENU): powermenu.c theme.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(XFT_CFLAGS) powermenu.c theme.o $(XFT_LIBS) -o $(POWERMENU)

$(LOCKSCREEN): lockscreen.c theme.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(XFT_CFLAGS) $(IMLIB2_CFLAGS) lockscreen.c theme.o $(XFT_LIBS) $(IMLIB2_LIBS) -lpam -o $(LOCKSCREEN)

$(BAR): bar.c theme.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(XFT_CFLAGS) bar.c theme.o $(XFT_LIBS) -lXrender -lfontconfig -o $(BAR)

$(STARLIGHT): starlight.c theme.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(XFT_CFLAGS) starlight.c theme.o $(XFT_LIBS) -lutil -o $(STARLIGHT)

$(COMPOSITOR): compositor.c
	$(CC) $(CFLAGS) $(LDFLAGS) compositor.c -o $(COMPOSITOR) $(LIBS) $(COMP_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(XFT_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC) $(LAUNCHER) $(POWERMENU) $(LOCKSCREEN) $(BAR) $(STARLIGHT) $(COMPOSITOR)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)
	install -m 755 $(LAUNCHER) $(DESTDIR)$(BINDIR)/$(LAUNCHER)
	install -m 755 $(POWERMENU) $(DESTDIR)$(BINDIR)/$(POWERMENU)
	install -m 755 $(LOCKSCREEN) $(DESTDIR)$(BINDIR)/$(LOCKSCREEN)
	install -m 755 $(BAR) $(DESTDIR)$(BINDIR)/$(BAR)
	install -m 755 $(STARLIGHT) $(DESTDIR)$(BINDIR)/$(STARLIGHT)
	install -m 755 $(COMPOSITOR) $(DESTDIR)$(BINDIR)/$(COMPOSITOR)
	install -d $(DESTDIR)$(XSESSIONSDIR)
	install -m 644 nebulawm.desktop $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop
	@if [ -n "$$SUDO_USER" ]; then \
		USER_HOME=$$(getent passwd $$SUDO_USER | cut -d: -f6); \
		install -d -m 755 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) $$USER_HOME/.config; \
		install -d -m 755 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) $$USER_HOME/.config/Nebula; \
		if [ ! -f $$USER_HOME/.config/Nebula/lockscreen.conf ]; then \
			install -m 644 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) lockscreen.conf $$USER_HOME/.config/Nebula/lockscreen.conf; \
		fi; \
		if [ ! -f $$USER_HOME/.config/Nebula/nebula.config ]; then \
			install -m 644 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) nebula.config $$USER_HOME/.config/Nebula/nebula.config; \
		fi; \
		if [ ! -f $$USER_HOME/.config/Nebula/bar.config ]; then \
			install -m 644 -o $$SUDO_USER -g $$(id -g $$SUDO_USER) bar.config $$USER_HOME/.config/Nebula/bar.config; \
		fi; \
	else \
		install -d -m 755 ~/.config/Nebula; \
		if [ ! -f ~/.config/Nebula/lockscreen.conf ]; then \
			install -m 644 lockscreen.conf ~/.config/Nebula/lockscreen.conf; \
		fi; \
		if [ ! -f ~/.config/Nebula/nebula.config ]; then \
			install -m 644 nebula.config ~/.config/Nebula/nebula.config; \
		fi; \
		if [ ! -f ~/.config/Nebula/bar.config ]; then \
			install -m 644 bar.config ~/.config/Nebula/bar.config; \
		fi; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)
	rm -f $(DESTDIR)$(BINDIR)/$(LAUNCHER)
	rm -f $(DESTDIR)$(BINDIR)/$(POWERMENU)
	rm -f $(DESTDIR)$(BINDIR)/$(LOCKSCREEN)
	rm -f $(DESTDIR)$(BINDIR)/$(BAR)
	rm -f $(DESTDIR)$(BINDIR)/$(STARLIGHT)
	rm -f $(DESTDIR)$(BINDIR)/$(COMPOSITOR)
	rm -f $(DESTDIR)$(XSESSIONSDIR)/nebulawm.desktop
