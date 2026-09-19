CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

TARGET := syspckg2
SYSROOT ?=
ARCH_LIBDIR ?= usr/lib/x86_64-linux-gnu

ifeq ($(strip $(SYSROOT)),)
PKG_CONFIG ?= pkg-config
LIBDNF5_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdnf5 2>/dev/null)
LIBDNF5_LIBS := $(shell $(PKG_CONFIG) --libs libdnf5 2>/dev/null)
else
LIBDNF5_CFLAGS := -I$(SYSROOT)/usr/include
LIBDNF5_LIBS := -L$(SYSROOT)/$(ARCH_LIBDIR) -Wl,-rpath-link,$(SYSROOT)/$(ARCH_LIBDIR) -Wl,--allow-shlib-undefined -ldnf5
endif

.PHONY: all clean test check

all: check $(TARGET)

check:
ifeq ($(strip $(SYSROOT)),)
	@pkg-config --exists libdnf5 || { \
		echo "ERR: libdnf5 development files not found."; \
		echo "Provide SYSROOT=<path> or install libdnf5-dev."; \
		exit 1; \
	}
else
	@test -f "$(SYSROOT)/usr/include/libdnf5/base/base.hpp" || { echo "ERR: Missing libdnf5 headers in $(SYSROOT)"; exit 1; }
	@test -e "$(SYSROOT)/$(ARCH_LIBDIR)/libdnf5.so" -o -e "$(SYSROOT)/$(ARCH_LIBDIR)/libdnf5.so.2" || { echo "ERR: Missing libdnf5 library in $(SYSROOT)"; exit 1; }
endif

$(TARGET): main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LIBDNF5_CFLAGS) $(LDFLAGS) -o $@ $< $(LIBDNF5_LIBS) $(LDLIBS)

test: $(TARGET)
	./$(TARGET) --version
	./$(TARGET) --help >/dev/null

clean:
	rm -f $(TARGET)
