if WITH_CUTTER
TESTS = run_test.sh

AM_TESTS_ENVIRONMENT = CUTTER="$(CUTTER)" XSLTPROC="$(XSLTPROC)"

AM_LDFLAGS = -module -rpath $(libdir) -avoid-version -no-undefined

AM_CPPFLAGS = $(CUTTER_CFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)

LIBS += $(CUTTER_LIBS)

check_LTLIBRARIES = test_urlfifo.la

test_urlfifo_la_SOURCES = test_urlfifo.c

test_urlfifo_la_LIBADD = ../liburlfifo.la

CLEANFILES = test_report.xml test_report_junit.xml

EXTRA_DIST = cutter2junit.xslt
endif
