#/usr/bin/make

# We rely on the following variables:
#   DESTDIR
#   NAME
#   VERSION
#
# We honour $PREFIX (but don't use it normally).
#
# We generate two trees, one for release and one for debug.
# They are both generated under $(DESTDIR) (and as a special
# case are not DESTDIR themselves).

.PHONY: all clean install install-release install-debug

# Supply default values.
NAME := name
VERSION := 0.0.0

install-release : FLAVOUR := release
install-release : SUFFIX :=

install-debug : FLAVOUR := debug
install-debug : SUFFIX := -debug

# The following variables definitions rely on $(SUFFIX) being defined,
# so make sure to use '=' to define them,
# for as late an evaluation as possible.
DST = $(DESTDIR)/$(NAME)$(SUFFIX)-dkms/usr/src/$(NAME)$(SUFFIX)-$(VERSION)
SHARE = $(DESTDIR)/$(NAME)$(SUFFIX)-dkms/usr/share/$(NAME)$(SUFFIX)-dkms

all:

clean:

install: install-release install-debug

install-release install-debug:
	@set -e; \
	install -d "`dirname $(DST)`"; \
	cp -a sgx "$(DST)"; \
	find "$(DST)" -type f -exec chmod u=rw,go=r "{}" \; ; \
	find "$(DST)" -type d -exec chmod u=rwx,go=rx "{}" \; ; \
	sed -i 's/__VERSION__/$(VERSION)/g; s/__SUFFIX__/$(SUFFIX)/g' "$(DST)/dkms.conf"; \
	sed -i 's/__FLAVOUR__/$(FLAVOUR)/g' "$(DST)/Makefile"; \
	install -d "$(SHARE)"; \
	install -m 755 "$(PREFIX)/usr/lib/dkms/common.postinst" "$(SHARE)/postinst"
