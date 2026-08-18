CC = cc
CFLAGS = -std=c17 -Wall -Wextra -Werror -pedantic -I.
LDFLAGS = -framework Foundation
CPPCHECK ?= cppcheck

PROG = xspawn
SRCS = xspawn.c label.c write-plist.c bootstrap-job.c

UNIT_LABEL = tests/unit/test-label
UNIT_PLIST = tests/unit/test-write-plist
UNIT_BOOT = tests/unit/test-bootstrap-job
UNIT_BINS = $(UNIT_LABEL) $(UNIT_PLIST) $(UNIT_BOOT)

HELLOWORLD = tests/functional/fixtures/helloworld
HELLOWORLD_SRC = tests/functional/fixtures/helloworld.c

.PHONY: all clean check lint check-cppcheck test test-unit test-functional

all: $(PROG) check

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

check-cppcheck:
	@command -v $(CPPCHECK) >/dev/null 2>&1 || { \
		echo "cppcheck not found: install it (see README)" >&2; exit 1; }
	$(CPPCHECK) --enable=warning,performance,portability \
		--error-exitcode=1 -I. $(SRCS)

lint: check-cppcheck
check: lint

$(UNIT_LABEL): tests/unit/test-label.c label.c label.h
	$(CC) $(CFLAGS) -o $@ tests/unit/test-label.c label.c

$(UNIT_PLIST): tests/unit/test-write-plist.c write-plist.c write-plist.h
	$(CC) $(CFLAGS) -o $@ tests/unit/test-write-plist.c write-plist.c \
		$(LDFLAGS)

$(UNIT_BOOT): tests/unit/test-bootstrap-job.c bootstrap-job.c bootstrap-job.h \
		xpc-pipe.h
	$(CC) $(CFLAGS) -o $@ tests/unit/test-bootstrap-job.c bootstrap-job.c \
		$(LDFLAGS)

$(HELLOWORLD): $(HELLOWORLD_SRC)
	$(CC) $(CFLAGS) -o $@ $(HELLOWORLD_SRC)

test-unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do \
		echo "==> $$t"; \
		./$$t || exit 1; \
	done

test-functional: $(PROG) $(HELLOWORLD)
	@./tests/functional/run-tests.sh ./$(PROG)

test: test-unit test-functional

clean:
	rm -f $(PROG) $(UNIT_BINS) $(HELLOWORLD)
