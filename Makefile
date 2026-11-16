CFLAGS = -g -O2 -Wall -Wextra
LDFLAGS =
LDLIBS = -lX11 -lXext -lXrender -lXrandr

prefix = /usr/local
bindir = $(prefix)/bin

CC = cc
RM = rm -f
INSTALL = install

all: haikei

install: haikei
	$(INSTALL) -d -m 755 '$(DESTDIR)$(bindir)'
	$(INSTALL) haikei '$(DESTDIR)$(bindir)'

clean:
	$(RM) haikei

uninstall:
	rm -f '$(DESTDIR)$(bindir)/haikei'

.PHONY: all install clean