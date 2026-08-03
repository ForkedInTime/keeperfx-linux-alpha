# Fork-local entry point for the Linux build.
#
# GNU make reads GNUmakefile in preference to Makefile, so a bare `make` in this
# Linux-only fork builds the Linux target instead of upstream's mingw/Windows one.
# Everything is forwarded to linux.mk unchanged.
#
# Upstream's Makefile and linux.mk are deliberately left untouched: both are
# upstream-owned and this fork takes weekly syncs, so keeping the fix in a file
# upstream does not have keeps the conflict surface at zero.
#
# Run `make -f Makefile <target>` if you ever genuinely want the Windows build.

# Goals to forward. `make` with no arguments means `all`.
GOALS := $(or $(MAKECMDGOALS),all)

CENTIJSON_LIB := deps/centijson/libjson.a

.PHONY: $(GOALS) forward-to-linux-mk check-shared-deps

# Depend on a single forwarding target so linux.mk is invoked once no matter how
# many goals were named on the command line.
$(GOALS): forward-to-linux-mk
	@:

forward-to-linux-mk: check-shared-deps
	@$(MAKE) -f linux.mk $(GOALS)

# The Windows and Linux builds unpack *different* prebuilt archives into the same
# deps/centijson directory (centijson-mingw32.tar.gz vs centijson-lin64.tar.gz).
# Building for Windows and then for Linux therefore leaves i386 COFF objects there,
# and the Linux link fails with a bare "cannot find -ljson" long after the mistake
# was made -- the compile stage still succeeds, so it reads as a code error.
# Drop the directory when it holds foreign objects; linux.mk re-extracts it.
#
# The first five bytes of a member are read rather than shelling out to file(1):
# `file -` needs a writable temporary directory to buffer piped input and fails
# outright without one. 7f 45 4c 46 is the ELF magic, and the fifth byte is
# EI_CLASS, 02 for 64-bit -- together enough to tell a Linux object from the i386
# COFF one the mingw build leaves behind.
check-shared-deps:
	@if [ -f '$(CENTIJSON_LIB)' ]; then \
		member="$$(ar t '$(CENTIJSON_LIB)' 2>/dev/null | head -1)"; \
		if [ -n "$$member" ]; then \
			magic="$$(ar p '$(CENTIJSON_LIB)' "$$member" 2>/dev/null \
				| head -c 5 | od -An -tx1 | tr -d ' \n')"; \
			if [ "$$magic" != '7f454c4602' ]; then \
				echo 'GNUmakefile: deps/centijson holds non-Linux objects (left by the mingw build); re-extracting'; \
				rm -rf deps/centijson; \
			fi; \
		fi; \
	fi
