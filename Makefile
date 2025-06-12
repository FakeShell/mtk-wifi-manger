CC = gcc

CFLAGS = `pkg-config --cflags gio-2.0 libandroid-properties` -Iinclude
LDFLAGS = `pkg-config --libs gio-2.0 libandroid-properties`

SOURCES = src/main.c src/dbus.c src/wmt.c src/wpa.c
TARGET = mtk-wifi-manager

PREFIX ?= /usr

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	install -d $(DESTDIR)$(PREFIX)/libexec
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/libexec/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/system
	install -m 0644 data/mtk-wifi-manager.service $(DESTDIR)$(PREFIX)/lib/systemd/system/
	install -d $(DESTDIR)$(PREFIX)/share/dbus-1/system-services
	install -m 0644 data/com.MediaTek.WiFiManager.service $(DESTDIR)$(PREFIX)/share/dbus-1/system-services/
	install -d $(DESTDIR)$(PREFIX)/share/dbus-1/system.d
	install -m 0644 data/com.MediaTek.WiFiManager.conf $(DESTDIR)$(PREFIX)/share/dbus-1/system.d/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/libexec/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/lib/systemd/system/mtk-wifi-manager.service
	rm -f $(DESTDIR)$(PREFIX)/share/dbus-1/system-services/com.MediaTek.WiFiManager.service
	rm -f $(DESTDIR)$(PREFIX)/share/dbus-1/system.d/com.MediaTek.WiFiManager.conf

.PHONY: all clean install uninstall
