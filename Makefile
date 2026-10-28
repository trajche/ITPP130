CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -Wall -Wextra -Wno-deprecated-declarations
LDLIBS   += $(shell cups-config --image --libs 2>/dev/null || echo -lcupsimage -lcups)

PREFIX   ?= /usr
DESTDIR  ?=
FILTERDIR = $(PREFIX)/lib/cups/filter
PPDDIR    = $(PREFIX)/share/ppd/munbyn

FILTER = rastertotspl
PPD    = build/munbyn-itpp130.ppd

.PHONY: all clean install uninstall test

all: build/$(FILTER) $(PPD)

build/$(FILTER): src/rastertotspl.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(PPD): ppd/munbyn-itpp130.drv | build
	ppdc -d build $<

build:
	mkdir -p build

install: all
	install -D -m 755 build/$(FILTER) $(DESTDIR)$(FILTERDIR)/$(FILTER)
	install -D -m 644 $(PPD) $(DESTDIR)$(PPDDIR)/munbyn-itpp130.ppd

uninstall:
	rm -f $(DESTDIR)$(FILTERDIR)/$(FILTER) $(DESTDIR)$(PPDDIR)/munbyn-itpp130.ppd

test: all
	test/run.sh build/$(FILTER) $(PPD)

clean:
	rm -rf build
