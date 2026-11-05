CC      = clang
CFLAGS  = -Wall -Wextra -std=c11 -g -Iinclude
SAN     = -fsanitize=address -fno-omit-frame-pointer
LDFLAGS = -pthread

SRCS   = $(shell find src -name '*.c')
HDRS   = $(shell find include src -name '*.h')
RUNNER = build/test_runner

.PHONY: all check test debug clean help

all: $(RUNNER)

$(RUNNER): tests/test_runner.c $(SRCS) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SAN) tests/test_runner.c $(SRCS) -o $@ $(LDFLAGS)

# 전체:                    make check
# 섹션별:                  make check-heap   check-object   check-gc
#                          make check-mark   check-sweep    check-integration   check-gen
# 여러 개 / 이름 일부:      make check F="heap object"     make check F=coalesce
check: $(RUNNER)
	@python3 tests/run.py $(F)

check-%: $(RUNNER)
	@python3 tests/run.py $*

# 테스트 하나를 줄 단위 출력과 함께 실행:   make test T=heap/split
test: $(RUNNER)
	@test -n "$(T)" || (echo "usage: make test T=heap/split" && exit 2)
	@$(RUNNER) tests/$(T:.gct=).gct

# lldb로 실행:                             make debug T=heap/split
debug: $(RUNNER)
	@test -n "$(T)" || (echo "usage: make debug T=heap/split" && exit 2)
	lldb $(RUNNER) -- tests/$(T:.gct=).gct

clean:
	rm -rf build

help:
	@sed -n 's/^# //p' Makefile
