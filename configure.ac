AC_PREREQ([2.69])
AC_INIT([Streamplayer], [0.1], [r.tiemann@ta-hifi.com])
AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([streamplayer.c])
AC_CONFIG_HEADERS([config.h])

AC_USE_SYSTEM_EXTENSIONS()

AC_ARG_VAR([CWARNINGS], [strict set of C compiler warnings])
AC_ARG_VAR([CRELAXEDWARNINGS], [options for relaxing some compiler warnings for various files])

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR
PKG_PROG_PKG_CONFIG([0.26])
AC_CHECK_PROGS([MARKDOWN], [markdown])
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AC_CHECK_PROGS([GDBUS_CODEGEN], [gdbus-codegen])

# Checks for libraries.
PKG_CHECK_MODULES([STREAMPLAYER_DEPENDENCIES], [gmodule-2.0 gio-2.0 gio-unix-2.0 gthread-2.0])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile Doxyfile])
AC_OUTPUT
