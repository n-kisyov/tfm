.PHONY: all clean release debug

TARGET  = tfm.exe
SRCDIR  = src
SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(SRCDIR)/%.o,$(SRCS))
CC      = gcc
CFLAGS  = -static -std=c11 -Wall -Wextra
LDFLAGS = -static -luser32 -lkernel32 -lshell32 -lshlwapi -lole32

all: release

release: CFLAGS += -O2 -s
release: $(TARGET)

debug: CFLAGS += -g -O0
debug: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -static -s -o $@ $^ $(LDFLAGS)
	@echo Build successful: $(TARGET)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f $(SRCDIR)/*.o $(TARGET)
	@echo Cleaned.
