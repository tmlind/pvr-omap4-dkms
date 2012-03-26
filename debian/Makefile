# This top-level makefile is called by DKMS.
# It must provide targets: 'all' and 'clean'.
# Making 'all' the default target is nice to have.
# This makefile must take the $KVERSION variable into account.

.PHONY: all clean

# Set to 'release' for "normal" build.
# Set to 'debug' for debug build.
# (Note that you will need libraries & apps compiled in
# 'debug' too for this to be usable.)
FLAVOUR := release

# This suffix is appended to module name.
SUFFIX-debug := -debug
SUFFIX-release :=
SUFFIX := $(SUFFIX-$(FLAVOUR))

# Define for verbose build.
#export V := 1

KVERSION ?= $(shell uname -r)
export CROSS_COMPILE :=
export DISCIMAGE := $(PWD)
export KERNELDIR := /usr/src/linux-headers-$(KVERSION)
DIR := eurasiacon/build/linux2/omap4430_linux

# We want to compile with two jobs per cpu.
JOBS := $(shell bash -c 'cpu=`egrep "^processor" /proc/cpuinfo |wc -l`; [ $$cpu == 0 ] && cpu=1; echo $$(($$cpu * 2))')

MAKE_OPTS-debug := BUILD=debug
MAKE_OPTS-release :=
MAKE_OPTS := -j$(JOBS) SUPPORT_V4L2_GFX=0 $(MAKE_OPTS-$(FLAVOUR))

MODULES := omapdrm_pvr # gfx_vout_mod

all:
	@set -e; \
	$(MAKE) -C $(DIR) $(MAKE_OPTS); \
	\
	for m in $(MODULES); do \
	    cp -v "eurasiacon/binary2_omap4430_linux_$(FLAVOUR)/target/$$m.ko" "$${m}$(SUFFIX).ko"; \
	done
# Note: we replace the 'make install' step by our cp for now.

clean:
	$(MAKE) -C $(DIR) $(MAKE_OPTS) clean \
	&& rm -f *.ko
