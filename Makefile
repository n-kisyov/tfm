.PHONY: all clean release debug

TARGET  = tfm.exe
SRCDIR  = src
SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(SRCDIR)/%.o,$(SRCS))
CC      = C:/msys64/ucrt64/bin/gcc.exe
CFLAGS  = -static -std=c11 -Wall -Wextra
LDFLAGS = -static -lssh2 -lz -lssl -lcrypto -lcrypt32 -lws2_32 \
          -luser32 -lkernel32 -lshell32 -lshlwapi -lole32

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
