#
# Copyright (C) 2015, 2016, 2017, 2018  T+A elektroakustik GmbH & Co. KG
#
# This file is part of T+A Streamplayer.
#
# T+A Streamplayer is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 3 as
# published by the Free Software Foundation.
#
# T+A Streamplayer is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.
#

SUBDIRS = . src dist tests

ACLOCAL_AMFLAGS = -I m4

if WITH_MARKDOWN
html_DATA = README.html
endif

EXTRA_DIST = README.md

CLEANFILES = README.html

EXTRA_DIST += \
    dbus_interfaces/extract_documentation.py \
    dbus_interfaces/de_tahifi_streamplayer.xml \
    dbus_interfaces/de_tahifi_artcache.xml \
    dbus_interfaces/de_tahifi_audiopath.xml \
    dbus_interfaces/de_tahifi_debug.xml

BUILT_SOURCES = versioninfo.h

CLEANFILES += $(BUILT_SOURCES)

versioninfo.h: versioninfo.cache
	(cd $(top_srcdir) && $(AUTOREVISION) -t h -o $(abs_top_builddir)/versioninfo.cache >$(abs_top_builddir)/$@)

.md.html:
	$(MARKDOWN) $< >$@

if CAN_BUILD_DEBIAN_PACKAGE
DEBIAN_PACKAGE_FILES = debian/changelog debian/compat debian/control debian/rules

EXTRA_DIST += $(DEBIAN_PACKAGE_FILES)

deb: dist
	rm -rf "$(distdir)"
	GZIP=$(GZIP_ENV) gzip -dc "$(distdir).tar.gz" | $(am__untar)
	$(am__cd) "$(distdir)" && DH_SHLIBDEPS_ARGS="$(DH_SHLIBDEPS_ARGS)" $(DPKG_BUILDPACKAGE) -rfakeroot -D -b -t$(host_alias)
	rm -rf "$(distdir)"
else
deb:
	@echo "Cannot build Debian package, requirements not met."
	exit 1
endif
