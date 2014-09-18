bin_PROGRAMS = streamplayer

html_DATA = README.html

EXTRA_DIST = README.md

CLEANFILES = README.html

streamplayer_SOURCES = \
    streamplayer.c

AM_CFLAGS = $(CWARNINGS)

AM_CPPFLAGS = $(STREAMPLAYER_DEPENDENCIES_CFLAGS)

streamplayer_LDADD = $(STREAMPLAYER_DEPENDENCIES_LIBS) libstreamplayer_dbus.a

noinst_LIBRARIES = libstreamplayer_dbus.a

nodist_libstreamplayer_dbus_a_SOURCES = streamplayer_dbus.c streamplayer_dbus.h

libstreamplayer_dbus_a_CFLAGS = $(CRELAXEDWARNINGS)

EXTRA_DIST += de_tahifi_streamplayer.xml

BUILT_SOURCES = $(nodist_libstreamplayer_dbus_a_SOURCES) streamplayer_dbus.stamp

CLEANFILES += $(BUILT_SOURCES)


streamplayer_dbus.c: streamplayer_dbus.stamp
streamplayer_dbus.h: streamplayer_dbus.stamp
streamplayer_dbus.stamp: de_tahifi_streamplayer.xml
	$(GDBUS_CODEGEN) --generate-c-code=streamplayer_dbus --c-namespace tdbus_splay --interface-prefix de.tahifi.Streamplayer. $<
	touch $@

.md.html:
	$(MARKDOWN) $< >$@
