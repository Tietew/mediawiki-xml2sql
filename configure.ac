dnl
dnl `configure' for xml2sql
dnl
AC_INIT([xml2sql],[0.5],[tietew@tietew.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([xml2sql.c])
AC_CONFIG_HEADER([config.h])

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL

AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h malloc.h stdlib.h string.h unistd.h])

AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

AC_SYS_LARGEFILE

AC_FUNC_MEMCMP
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([memset strcasecmp strncasecmp stricmp strnicmp strdup strerror strspn strstr strtoul mkstemp])

dnl ** zlib support

AC_ARG_WITH([zlib],
[AC_HELP_STRING([--with-zlib=DIRECTORY],[base directory for zlib])
AC_HELP_STRING([--without-zlib],[disable zlib support])],[
	if test "$withval" != no; then
		CFLAGS="$CFLAGS -I$withval/include"
		LDFLAGS="$LDFLAGS -L$withval/lib"
	fi
])
if test "$with_zlib" != no; then
	AC_CHECK_LIB([z],[deflate],,[AC_MSG_WARN([disabled zlib support.])])
fi

dnl ** expat

AC_ARG_WITH(expat,
[AC_HELP_STRING([--with-expat=DIRECTORY],[base directory for expat])],[
	if test "$withval" != no; then
		CFLAGS="$CFLAGS -I$withval/include"
		LDFLAGS="$LDFLAGS -L$withval/lib"
	fi
])
AC_CHECK_LIB([expat],[XML_StopParser],,[AC_MSG_ERROR([expat 1.95.8 or later is required.])])

dnl ** GNU getopt

AC_ARG_WITH(bundled-getopt,
[AC_HELP_STRING([--with-bundled-getopt],[use bundled getopt])])
if test "$with_bundled_getopt" = no || test -z "$with_bundled_getopt"; then
	AC_CHECK_HEADERS([getopt.h])
	AC_CHECK_FUNCS([getopt_long])
fi
AM_CONDITIONAL([USE_BUNDLED_GETOPT],[test "$ac_cv_func_getopt_long" != yes])

AC_CONFIG_FILES([getopt/Makefile Makefile])
AC_OUTPUT
