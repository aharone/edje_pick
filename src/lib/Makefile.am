MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = \
-I$(top_srcdir)/src/include \
-I$(top_builddir)/src/include \
-I/home/aharon/e17/efl/src/bin/edje \
-I/home/aharon/e17/efl/src/lib/edje \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/$(PACKAGE)\" \
@EFL_CFLAGS@

includesdir = $(includedir)/edje_pick-@VMAJ@
includes_HEADERS = \
		  Edje_Pick.h

lib_LTLIBRARIES = libedjepick.la

libedjepick_la_SOURCES = \
edje_pick_core.c \
Edje_Pick.h
libedjepick_la_LIBADD = @EFL_LIBS@
libedjepick_la_CFLAGS = @EFL_CFLAGS@
libedjepick_la_LDFLAGS = -no-undefined @lt_enable_auto_import@ -version-info @version_info@ @release_info@
