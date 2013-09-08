MAINTAINERCLEANFILES = Makefile.in

bin_PROGRAMS = edje_pick
bin_PROGRAMS += gpick

edje_pick_SOURCES = edje_pick.c \
$(top_srcdir)src/lib/Edje_Pick.h

gpick_SOURCES = gpick.c \
$(top_srcdir)src/lib/Edje_Pick.h

AM_CPPFLAGS = \
-I$(top_srcdir)/src/lib \
-I$(top_srcdir)/src/include \
-I$(top_builddir)/src/include \
-I$HOME/e17/efl/src/bin/edje \
-I$HOME/e17/efl/src/lib/edje \
-I$HOME/e17/elementary/src/ \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/$(PACKAGE)\" \
@EFL_CFLAGS@

edje_pick_LDADD = $(top_builddir)/src/lib/libedjepick.la @EFL_LIBS@
gpick_LDADD = $(top_builddir)/src/lib/libedjepick.la @EFL_LIBS@
