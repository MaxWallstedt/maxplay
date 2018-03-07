INSTALL = install
MKDIR = mkdir
RM = rm -f
CC = gcc
CFLAGS = -g -pedantic -Wall -Wextra -Werror `pkg-config --cflags libpulse-simple`
LDFLAGS = `pkg-config --libs libpulse-simple`

TARGET = maxplay
PREFIX = $(HOME)

all: $(TARGET)

$(TARGET): main.o wav.o
	@echo "LD $@"
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

main.o: main.c wav.h
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -o $@ $<

wav.o: wav.c wav.h
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -o $@ $<

install: $(PREFIX)/bin/$(TARGET)

$(PREFIX)/bin/$(TARGET): $(TARGET) $(PREFIX)/bin
	@echo "INSTALL $@"
	@$(INSTALL) $< $@

$(PREFIX)/bin:
	@echo "MKDIR $@"
	@$(MKDIR) $@

.PHONY: clean uninstall

clean:
	@echo "RM $(TARGET)"
	@$(RM) $(TARGET)
	@echo "RM main.o"
	@$(RM) main.o
	@echo "RM wav.o"
	@$(RM) wav.o

uninstall:
	@echo "RM $(PREFIX)/bin/$(TARGET)"
	@$(RM) $(PREFIX)/bin/$(TARGET)
