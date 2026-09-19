CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

TARGET := syspckg2
SYSROOT ?=
ARCH_LIBDIR ?= usr/lib/x86_64-linux-gnu
PKG_CONFIG ?= pkg-config

ifeq ($(strip $(SYSROOT)),)
LIBDNF5_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdnf5 2>/dev/null)
LIBDNF5_LIBS := $(shell $(PKG_CONFIG) --libs libdnf5 2>/dev/null)
else
SYSROOT_PKG_CONFIG_LIBDIR := $(SYSROOT)/$(ARCH_LIBDIR)/pkgconfig:$(SYSROOT)/usr/share/pkgconfig
SYSROOT_PKG_CONFIG := PKG_CONFIG_SYSROOT_DIR="$(SYSROOT)" PKG_CONFIG_LIBDIR="$(SYSROOT_PKG_CONFIG_LIBDIR)" $(PKG_CONFIG)
LIBDNF5_CFLAGS := $(shell $(SYSROOT_PKG_CONFIG) --cflags libdnf5 2>/dev/null)
LIBDNF5_LIBS := $(shell $(SYSROOT_PKG_CONFIG) --libs libdnf5 2>/dev/null)
endif

.PHONY: all clean test check

all: check $(TARGET)

check:
	@command -v "$(PKG_CONFIG)" >/dev/null 2>&1 || { \
		echo "ERR: pkg-config not found."; \
		exit 1; \
	}
ifeq ($(strip $(SYSROOT)),)
	@$(PKG_CONFIG) --exists libdnf5 || { \
		echo "ERR: libdnf5 development files not found."; \
		echo "Provide SYSROOT=<path> or install libdnf5 development files."; \
		exit 1; \
	}
else
	@test -f "$(SYSROOT)/usr/include/libdnf5/base/base.hpp" || { echo "ERR: Missing libdnf5 headers in $(SYSROOT)"; exit 1; }
	@test -f "$(SYSROOT)/$(ARCH_LIBDIR)/pkgconfig/libdnf5.pc" || { echo "ERR: Missing libdnf5.pc in $(SYSROOT)"; exit 1; }
	@$(SYSROOT_PKG_CONFIG) --exists libdnf5 || { \
		echo "ERR: pkg-config cannot resolve libdnf5 and its dependencies from $(SYSROOT)."; \
		$(SYSROOT_PKG_CONFIG) --print-errors --exists libdnf5 || true; \
		exit 1; \
	}
endif

$(TARGET): main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LIBDNF5_CFLAGS) $(LDFLAGS) -o $@ $< $(LIBDNF5_LIBS) $(LDLIBS)

test: $(TARGET)
	./$(TARGET) --version
	./$(TARGET) --help >/dev/null

clean:
	rm -f $(TARGET)
