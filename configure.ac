dnl Copyright (C) 2015, 2016, 2017  T+A elektroakustik GmbH & Co. KG
dnl
dnl This file is part of T+A Streamplayer.
dnl
dnl T+A Streamplayer is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License, version 3 as
dnl published by the Free Software Foundation.
dnl
dnl T+A Streamplayer is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with T+A Streamplayer.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.69])
AC_INIT([Streamplayer], [3.5], [r.tiemann@ta-hifi.com])
AC_CONFIG_AUX_DIR([config])
AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([streamplayer.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_USE_SYSTEM_EXTENSIONS()

AC_ARG_VAR([CWARNINGS], [strict set of C compiler warnings])
AC_ARG_VAR([CRELAXEDWARNINGS], [options for relaxing some compiler warnings for various files])
AC_ARG_VAR([DEBIAN_SYSROOT_DIR], [path to Debian system root to use when cross-building a Debian package])
AC_ARG_VAR([DEBIAN_SYSROOT_LIBPATH], [colon-separated list of paths to search for libraries when cross-building a Debian package])

AC_ARG_ENABLE([valgrind],
              [AS_HELP_STRING([--disable-valgrind],
                              [disable an extra run of unit tests with Valgrind on ``make check''])],
              [],
              [enable_valgrind=yes])

# Checks for programs.
AC_PROG_CC
AM_PROG_AR
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG([0.26])
AC_CHECK_PROGS([MARKDOWN], [markdown])
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AC_CHECK_PROGS([GDBUS_CODEGEN], [gdbus-codegen])
AC_CHECK_PROGS([DPKG_BUILDPACKAGE], [dpkg-buildpackage])
AC_CHECK_PROGS([DPKG_ARCHITECTURE], [dpkg-architecture])
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
AC_CHECK_PROGS([VALGRIND], [valgrind])
AC_CHECK_PROGS([AUTOREVISION], [autorevision])

m4_ifdef([AC_CHECK_CUTTER],
[
AC_CHECK_CUTTER
AC_CHECK_GCUTTER
AC_CHECK_COVERAGE
],
[ac_cv_use_cutter=no])

# Checks for libraries.
PKG_CHECK_MODULES([STREAMPLAYER_DEPENDENCIES], [gmodule-2.0 gio-2.0 gio-unix-2.0 gthread-2.0 gstreamer-tag-1.0 gstreamer-plugins-base-1.0])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stdlib.h string.h sys/ioctl.h sys/time.h syslog.h unistd.h])

m4_ifdef([AC_CHECK_CUTTER],
[
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$pkg_cv_CUTTER_CFLAGS"
AC_CHECK_HEADER([cutter.h], [], [ac_cv_use_cutter=no])
CPPFLAGS="$save_CPPFLAGS"
])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_C_RESTRICT
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([clock_gettime memset mkdir munmap realpath rmdir strdup strerror])

# Debian architecture name for debian/control.in
AS_IF([test "x$ac_cv_prog_DPKG_ARCHITECTURE" != x],
      [DEBIAN_ARCHITECTURE_NAME=$($ac_cv_prog_DPKG_ARCHITECTURE -qDEB_HOST_ARCH -t$host_alias)],
      [DEBIAN_ARCHITECTURE_NAME=])
AC_SUBST([DEBIAN_ARCHITECTURE_NAME])

# arguments for dh_shlibdeps while building Debian package
AS_IF([test "x$DEBIAN_SYSROOT_DIR" != x],
      [DH_SHLIBDEPS_SYSROOT_ARGS="--dpkg-shlibdeps-params='--admindir=\$(DEBIAN_SYSROOT_DIR)/var/lib/dpkg --ignore-missing-info'"],
      [DH_SHLIBDEPS_SYSROOT_ARGS="--dpkg-shlibdeps-params='--ignore-missing-info'"])
AS_IF([test "x$DEBIAN_SYSROOT_LIBPATH" != x],
      [DH_SHLIBDEPS_LIBPATH_ARGS="-l\$(DEBIAN_SYSROOT_LIBPATH)"],
      [DH_SHLIBDEPS_LIBPATH_ARGS=])
DH_SHLIBDEPS_ARGS="$DH_SHLIBDEPS_SYSROOT_ARGS $DH_SHLIBDEPS_LIBPATH_ARGS"
AC_SUBST([DH_SHLIBDEPS_ARGS])

AM_CONDITIONAL([WITH_CUTTER], [test "x$ac_cv_use_cutter" = "xyes"])
AM_CONDITIONAL([WITH_VALGRIND], [test "x$enable_valgrind" = "xyes"])
AM_CONDITIONAL([CAN_BUILD_DEBIAN_PACKAGE], [test "x$ac_cv_prog_DPKG_BUILDPACKAGE" != x && test "x$DEBIAN_ARCHITECTURE_NAME" != x])

AC_CONFIG_FILES([Makefile tests/Makefile dist/Makefile Doxyfile])
AC_CONFIG_FILES([versioninfo.cache])
AC_CONFIG_FILES([debian/control])
AC_CONFIG_FILES([tests/run_test.sh], [chmod +x tests/run_test.sh])
AC_CONFIG_FILES([tests/valgrind.sh], [chmod +x tests/valgrind.sh])
AC_OUTPUT
