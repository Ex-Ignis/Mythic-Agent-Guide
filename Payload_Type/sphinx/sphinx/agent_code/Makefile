# Core Makefile — invoked by the builder (sphinx.py) inside the Docker container.
# DEBUG=1 → console window visible (for testing); without DEBUG → -mwindows (hidden window)

CC     = x86_64-w64-mingw32-gcc
CFLAGS = -Os -s -ffunction-sections -Wl,--gc-sections -IInclude -std=c11
LIBS   = -ladvapi32 -lwinhttp -liphlpapi -lsecur32
SRC    = $(wildcard Src/*.c)

ifdef DEBUG
CFLAGS += -g
else
CFLAGS += -mwindows
endif

sphinx.exe: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -f sphinx.exe

.PHONY: clean
