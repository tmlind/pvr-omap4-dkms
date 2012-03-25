#
# Copyright (C) Imagination Technologies Ltd. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
# 
# This program is distributed in the hope it will be useful but, except 
# as otherwise stated in writing, without any warranty; without even the 
# implied warranty of merchantability or fitness for a particular purpose. 
# See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
# 
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# Imagination Technologies Ltd. <gpl-support@imgtec.com>
# Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
# 
#

$(if $(KERNELDIR),,$(error KERNELDIR must be set to obtain a version))

override KERNEL_VERSION := \
 $(shell grep "^VERSION = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_PATCHLEVEL := \
 $(shell grep "^PATCHLEVEL = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_SUBLEVEL := \
 $(shell grep "^SUBLEVEL = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_EXTRAVERSION := \
 $(shell grep "^EXTRAVERSION = " $(KERNELDIR)/Makefile | cut -f3 -d' ')

# The base ID doesn't have to be accurate; we only use it for
# feature checks which will not care about extraversion bits
#
override KERNEL_BASE_ID := \
 $(KERNEL_VERSION).$(KERNEL_PATCHLEVEL).$(KERNEL_SUBLEVEL)

# UTS_RELEASE is more accurate, if we can find it
# If we can't, fall back to manufacturing the version
#
KERNEL_ID ?= \
 $(shell grep -h '\#define UTS_RELEASE' \
	$(KERNELDIR)/include/*/* | cut -f3 -d' ' | sed s/\"//g)

ifeq ($(strip $(KERNEL_ID)),)
# kernel.release, it it exists, contains what UTS_RELEASE would have
# been set to.
# 
KERNEL_ID := \
 $(shell cat $(KERNELDIR)/include/config/kernel.release 2>/dev/null)
endif

ifeq ($(strip $(KERNEL_ID)),)
KERNEL_ID := \
 $(KERNEL_VERSION).$(KERNEL_PATCHLEVEL).$(KERNEL_SUBLEVEL)$(KERNEL_EXTRAVERSION)
endif
