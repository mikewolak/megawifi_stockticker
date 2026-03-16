################################################################################
# stock_ticker_sim — ncurses + libcurl desktop simulation
# Same 40×28 screen, same types, same logic as the Sega Genesis ROM.
#
# Prerequisites (macOS):  brew install ncurses curl
# Build:  make
# Run:    ./stock_ticker_sim
################################################################################

CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -Wno-unused-parameter
CFLAGS += $(shell pkg-config --cflags ncurses  2>/dev/null)
CFLAGS += $(shell pkg-config --cflags libcurl  2>/dev/null)

LIBS   = $(shell pkg-config --libs ncurses  2>/dev/null || echo -lncurses)
LIBS  += $(shell pkg-config --libs libcurl  2>/dev/null || curl-config --libs 2>/dev/null || echo -lcurl)
LIBS  += -lpthread

TARGET = stock_ticker_sim
SRC    = stock_ticker_sim.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)
