SERVICE := fluxmail
DESTDIR ?= dist_root
SERVICEDIR ?= /srv/$(SERVICE)

.PHONY: build install

build:
	$(MAKE) -C src

install: build
	mkdir -p $(DESTDIR)$(SERVICEDIR)
	cp src/fluxmail $(DESTDIR)$(SERVICEDIR)/
	mkdir -p $(DESTDIR)/etc/systemd/system
	cp src/fluxmail@.service $(DESTDIR)/etc/systemd/system/
	cp src/fluxmail.socket $(DESTDIR)/etc/systemd/system/
	cp src/system-fluxmail.slice $(DESTDIR)/etc/systemd/system/
