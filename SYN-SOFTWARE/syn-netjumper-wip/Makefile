# SYN-NETJUMPER: `make`, then `make install` (into ~/.local unless PREFIX says otherwise)
PREFIX ?= $(HOME)/.local
PKGS = gtk+-3.0 webkit2gtk-4.1 libsecret-1 libsoup-3.0
CFLAGS += -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

syn-netjumper: netjumper.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: syn-netjumper
	install -Dm755 syn-netjumper $(PREFIX)/bin/syn-netjumper
	install -Dm644 syn-netjumper.svg $(PREFIX)/share/icons/hicolor/scalable/apps/syn-netjumper.svg
	sed 's|@BIN@|$(PREFIX)/bin/syn-netjumper|' syn-netjumper.desktop.in > com.syntax990.synnetjumper.desktop
	install -Dm644 com.syntax990.synnetjumper.desktop $(PREFIX)/share/applications/com.syntax990.synnetjumper.desktop
	rm -f com.syntax990.synnetjumper.desktop

clean:
	rm -f syn-netjumper

.PHONY: install clean
