## Process this file with automake to produce Makefile.in

EXTRA_DIST = keywords config.h.vc Makefile.vc win32

if USE_BUNDLED_GETOPT
SUBDIRS = getopt
LDADD = $(top_builddir)/getopt/libgetopt.a
endif
DIST_SUBDIRS = getopt

bin_PROGRAMS = xml2sql
xml2sql_SOURCES = xml2sql.c md5.c md5.h st.c st.h random.c missing.h
EXTRA_xml2sql_SOURCES = mediawiki.c

mediawiki.c: keywords
	@-$(RM) $@
	gperf -gptoC -Nlu_elt $? > $@
