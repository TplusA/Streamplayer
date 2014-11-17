if WITH_CUTTER
TESTS = run_test.sh

AM_TESTS_ENVIRONMENT = CUTTER="$(CUTTER)" XSLTPROC="$(XSLTPROC)"

AM_LDFLAGS = -module -rpath $(libdir) -avoid-version -no-undefined

AM_CPPFLAGS = $(CUTTER_CFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)

AM_CFLAGS = $(CWARNINGS)

LIBS += $(CUTTER_LIBS)

check_LTLIBRARIES = test_urlfifo.la

test_urlfifo_la_SOURCES = test_urlfifo.c

test_urlfifo_la_LIBADD = ../liburlfifo.la

test_urlfifo_la_CFLAGS = $(AM_CFLAGS)

CLEANFILES = test_report.xml test_report_junit.xml

EXTRA_DIST = cutter2junit.xslt

check-local: check-TESTS
	@if $(GREP) -w cutter $(TEST_LOGS); then \
	    echo "Unit tests failed (check log)"; \
	    exit 1; \
	fi
endif
