# vidwizard — C++23 / gnu++23, gcc/g++ only.
# Default: debug + ASan/UBSan.  make release  /  make profile  /  make test  /  make verify

CXX ?= g++
CC  ?= gcc
CXX := $(CXX) -Wl,--as-needed -Wl,-O1 -Wl,--hash-style=gnu -Wl,-z,relro
CC  := $(CC)  -Wl,--as-needed -Wl,-O1 -Wl,--hash-style=gnu -Wl,-z,relro

MAKEFLAGS += --no-print-directory

export PKG_CONFIG_PATH := $(HOME)/.local/share/pkgconfig:$(HOME)/.local/lib64/pkgconfig:$(HOME)/.local/lib/pkgconfig:$(PKG_CONFIG_PATH)

FFMPEG_PKGS := libavfilter libavformat libavcodec libavutil libswscale libswresample
FFMPEG_CFLAGS := $(shell pkg-config --cflags $(FFMPEG_PKGS))
FFMPEG_LIBS   := -Wl,--push-state,--as-needed $(shell pkg-config --libs $(FFMPEG_PKGS)) -Wl,--pop-state

CATCH_CFLAGS := $(shell pkg-config --cflags catch2-with-main)
CATCH_LIBS   := $(shell pkg-config --libs catch2-with-main)

WARN := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnull-dereference \
        -Wdouble-promotion -Wformat=2 -Wformat-security -Wstack-protector

INCLUDES := -Iinclude

CXXFLAGS_COMMON := -std=gnu++23 -pipe $(WARN) -fstack-protector-strong \
                   -fno-omit-frame-pointer \
                   -ffunction-sections -fdata-sections \
                   $(INCLUDES) $(FFMPEG_CFLAGS)
CFLAGS_COMMON := -std=gnu23 -pipe $(WARN) -fstack-protector-strong \
                 -fno-omit-frame-pointer \
                 -ffunction-sections -fdata-sections \
                 $(INCLUDES)

CXXFLAGS_OPTIMIZED := -O3 -march=x86-64 -mtune=generic -fno-omit-frame-pointer

CXXFLAGS_DEBUG := $(CXXFLAGS_COMMON) -g3 -O0 -fsanitize=address,undefined
CFLAGS_DEBUG   := $(CFLAGS_COMMON)   -g3 -O0 -fsanitize=address,undefined
LDFLAGS_DEBUG  := -fsanitize=address,undefined -rdynamic -Wl,--gc-sections

CXXFLAGS_RELEASE := $(CXXFLAGS_COMMON) -DNDEBUG -D_FORTIFY_SOURCE=2 $(CXXFLAGS_OPTIMIZED)
CFLAGS_RELEASE   := $(CFLAGS_COMMON)   -DNDEBUG -D_FORTIFY_SOURCE=2 $(CXXFLAGS_OPTIMIZED)
LDFLAGS_RELEASE  := -Wl,--gc-sections -Wl,-z,now

CXXFLAGS_PROFILE := $(CXXFLAGS_COMMON) -DNDEBUG $(CXXFLAGS_OPTIMIZED) -g -pg -fno-inline
CFLAGS_PROFILE   := $(CFLAGS_COMMON)   -DNDEBUG $(CXXFLAGS_OPTIMIZED) -g -pg -fno-inline
LDFLAGS_PROFILE  := -pg -Wl,--gc-sections

BUILD ?= debug
ifeq ($(BUILD),release)
  CXXFLAGS := $(CXXFLAGS_RELEASE)
  CFLAGS   := $(CFLAGS_RELEASE)
  LDFLAGS  := $(LDFLAGS_RELEASE)
else ifeq ($(BUILD),profile)
  CXXFLAGS := $(CXXFLAGS_PROFILE)
  CFLAGS   := $(CFLAGS_PROFILE)
  LDFLAGS  := $(LDFLAGS_PROFILE)
else
  CXXFLAGS := $(CXXFLAGS_DEBUG)
  CFLAGS   := $(CFLAGS_DEBUG)
  LDFLAGS  := $(LDFLAGS_DEBUG)
endif

BUILDDIR := build/$(BUILD)
BINDIR   := bin/$(BUILD)

LIB_SRCS_CXX := src/log.cpp src/threads.cpp src/cli.cpp src/paths.cpp \
                src/filter_spec.cpp src/transcode.cpp src/explode.cpp src/pipeline.cpp
LIB_SRCS_C   := src/parse_time.c
LIB_OBJS := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRCS_CXX)) \
            $(patsubst src/%.c,$(BUILDDIR)/%.o,$(LIB_SRCS_C))

MAIN_OBJ := $(BUILDDIR)/main.o
TARGET   := $(BINDIR)/vidwizard

TEST_SRCS := tests/test_parse_time.cpp tests/test_cli.cpp tests/test_paths.cpp \
             tests/test_filter_spec.cpp tests/test_integration.cpp
TEST_OBJS := $(patsubst tests/%.cpp,$(BUILDDIR)/tests/%.o,$(TEST_SRCS))
TEST_BIN  := $(BINDIR)/run_tests

BUILD_FLAGS := -s V=0 -j$(shell nproc 2>/dev/null || echo 1)

.PHONY: all clean release profile test tests verify tags man install \
        testdata compile_flags

all: $(TARGET)

$(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CATCH_CFLAGS) -c $< -o $@

$(TARGET): $(LIB_OBJS) $(MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(FFMPEG_LIBS)
	@ln -sfn $(TARGET) vidwizard

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(CATCH_LIBS) $(FFMPEG_LIBS)

testdata: testdata/clip.mp4 testdata/red.mp4 testdata/stereo.mp4

testdata/clip.mp4:
	@mkdir -p testdata
	ffmpeg -y -hide_banner -loglevel error \
	  -f lavfi -i testsrc=duration=2:size=320x240:rate=10 \
	  -f lavfi -i sine=frequency=440:duration=2 \
	  -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest $@

testdata/red.mp4:
	@mkdir -p testdata
	ffmpeg -y -hide_banner -loglevel error \
	  -f lavfi -i color=c=red:duration=1:size=64x64:rate=5 \
	  -c:v libx264 -pix_fmt yuv420p $@

testdata/stereo.mp4:
	@mkdir -p testdata
	ffmpeg -y -hide_banner -loglevel error \
	  -f lavfi -i testsrc=duration=2:size=320x240:rate=10 \
	  -f lavfi -i sine=frequency=440:duration=2,aformat=channel_layouts=stereo \
	  -c:v libx264 -pix_fmt yuv420p -c:a aac -ac 2 -shortest $@

test: $(TARGET) $(TEST_BIN) testdata
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	LSAN_OPTIONS=suppressions=$(CURDIR)/lsan.supp \
	VIDWIZARD_BIN=$(abspath $(TARGET)) ./$(TEST_BIN)
tests: test

verify: test
	$(MAKE) -C formal verify

release:
	$(MAKE) $(BUILD_FLAGS) clean
	$(MAKE) $(BUILD_FLAGS) BUILD=release all
	@echo "Release binary built: bin/release/vidwizard"

profile:
	$(MAKE) $(BUILD_FLAGS) clean
	$(MAKE) $(BUILD_FLAGS) BUILD=profile all
	@echo "Profile binary built: bin/profile/vidwizard"

tags:
	ctags -R --languages=C,C++ --exclude=.git --exclude=build --exclude=bin -f tags .

man: man/vidwizard.1
	@mandoc -T lint man/vidwizard.1

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/vidwizard
	install -d $(DESTDIR)/usr/local/share/man/man1
	install -m 644 man/vidwizard.1 $(DESTDIR)/usr/local/share/man/man1/vidwizard.1

compile_flags:
	@printf '%s\n' -std=gnu++23 -Iinclude $(FFMPEG_CFLAGS) > compile_flags.txt

clean:
	rm -rf build bin vidwizard tests/run_tests gmon.out profile.txt
	rm -f testdata/clip.mp4 testdata/red.mp4 testdata/stereo.mp4
	rm -rf testdata/out

ifeq ($(V),1)
  # verbose: default recipe echo
else
  .SILENT:
endif
