bin_PROGRAMS = streamplayer

html_DATA = README.html

EXTRA_DIST = README.md

CLEANFILES = README.html

streamplayer_SOURCES = \
    streamplayer.c messages.c messages.h dbus_iface.c dbus_iface.h \
    streamtime.h

AM_CFLAGS = $(CWARNINGS)

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) libstreamplayer_dbus.a

noinst_LIBRARIES = libstreamplayer_dbus.a

nodist_libstreamplayer_dbus_a_SOURCES = streamplayer_dbus.c streamplayer_dbus.h

libstreamplayer_dbus_a_CFLAGS = $(CRELAXEDWARNINGS)

EXTRA_DIST += dbus_interfaces/extract_documentation.py dbus_interfaces/de_tahifi_streamplayer.xml

BUILT_SOURCES = $(nodist_libstreamplayer_dbus_a_SOURCES) de_tahifi_streamplayer.h de_tahifi_streamplayer.md streamplayer_dbus.stamp

CLEANFILES += $(BUILT_SOURCES)


de_tahifi_streamplayer.h: streamplayer_dbus.stamp
de_tahifi_streamplayer.md: streamplayer_dbus.stamp
streamplayer_dbus.c: streamplayer_dbus.stamp
streamplayer_dbus.h: streamplayer_dbus.stamp
streamplayer_dbus.stamp: dbus_interfaces/de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=streamplayer_dbus --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	$(abs_srcdir)/dbus_interfaces/extract_documentation.py -i $< -o de_tahifi_streamplayer.md -H de_tahifi_streamplayer.h -c tdbus_splay -s de.tahifi.Streamplayer. -n "$(PACKAGE_NAME)"
	touch $@

.md.html:
	$(MARKDOWN) $< >$@
