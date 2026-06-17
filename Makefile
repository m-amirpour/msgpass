CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS := -lpthread

INCLUDE := -Iinclude -Isrc

BUILDDIR := build

# All platform-independent source files.
COMMON_SRCS := \
    src/mp_log.c \
    src/mp_buf.c \
    src/mp_protocol.c \
    src/mp_queue.c \
    src/mp_executor.c \
    src/mp_client_state.c \
    src/mp_dispatcher.c \
    src/mp_server_st.c \
    src/mp_server_mt.c

# POSIX-specific sources.
OS_SRCS := \
    src/os/os_socket_posix.c \
    src/os/os_thread_posix.c \
    src/os/os_process_posix.c \
    src/os/os_time.c

ALL_SRCS := $(COMMON_SRCS) $(OS_SRCS)

COMMON_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(ALL_SRCS))

SERVER_SRCS := $(ALL_SRCS) src/mp_server.c
CLIENT_SRCS := $(ALL_SRCS) src/mp_client.c

SERVER_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(CLIENT_SRCS))

.PHONY: all clean tests

all: $(BUILDDIR)/msgpass_server $(BUILDDIR)/msgpass_client

$(BUILDDIR)/msgpass_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/msgpass_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $<

# Test binaries
TEST_BINS := \
    $(BUILDDIR)/test_protocol \
    $(BUILDDIR)/test_queue \
    $(BUILDDIR)/test_buf \
    $(BUILDDIR)/test_executor

tests: $(TEST_BINS)
	@echo ""
	@for t in $(TEST_BINS); do echo "Running $$t..."; $$t; echo ""; done

$(BUILDDIR)/test_protocol: tests/test_protocol.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) -Itests -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_queue: tests/test_queue.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) -Itests -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_buf: tests/test_buf.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) -Itests -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_executor: tests/test_executor.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) -Itests -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)