DEFTARGS = PhatBak

## get the latest git tag and create C++ macros for source
VERSION := $(perl {@tags = grep /^v/, `git tag --merged`; $_ = pop @tags; chomp; s/^v//;$_})
VERSION_MAJOR := $(perl {my @parts = split /\./, $VERSION; $parts[0];})
VERSION_MINOR := $(perl {my @parts = split /\./, $VERSION; $parts[1];})
VERSION_ARG = -DVERSION_MAJOR=$(VERSION_MAJOR) -DVERSION_MINOR=$(VERSION_MINOR)

MYCFLAGS  = -Wall -Werror -Wno-error=parentheses
#MYCFLAGS += -rdynamic
DBGMSG = -DDBGMSG
ifdef DBG
    # DBG=1 creates debuggable code else fastest
    MYCFLAGS  += -g -O0 -fweb $(DBGMSG) -DLOCKCHECK
else
    MYCFLAGS  += -O3 -funroll-loops -finline-functions -fno-diagnostics-color -DNDEBUG
endif

CXXFLAGS =
CPPVERSION = -std=c++23
CPPFLAGS += $(CPPVERSION) $(MYCFLAGS)
LDFLAGS  += $(CPPVERSION) -lpthread -lstdc++fs -lzstd -lmhash
LDFLAGS  += -lstdc++exp # needed for now for c++23 stdlib stacktrace
LDFLAGS  += -rdynamic
LDFLAGS  += -lacl
LDFLAGS  += -lstdc++fs

ifneq ($(CLANG),)
    CC  = clang -Wno-ignored-optimization-argument
    CXX = clang -Wno-ignored-optimization-argument
    LD  = clang
endif

ifdef PROF
    LDFLAGS += -lprofiler
endif

.PHONY: default
default:  $(DEFTARGS)

Opts.o:  CXXFLAGS=$(VERSION_ARG)

.PHONY: clean
clean:
	rm -f *.o
	rm -f tartar ttdump
        rm -rf .makepp
        rm -f PhatBak UtilsTest
