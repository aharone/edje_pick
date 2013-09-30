MAINTAINERCLEANFILES = Makefile.in

bin_PROGRAMS = edje_pick
bin_PROGRAMS += gpick

edje_pick_SOURCES = edje_pick.c

gpick_SOURCES = gpick.c

AM_CPPFLAGS = \
-I$(top_srcdir)/src/lib \
-I$(top_srcdir)/src/include \
-I$(top_builddir)/src/include \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/$(PACKAGE)\" \
@EFL_CFLAGS@

edje_pick_LDADD = @EFL_LIBS@
gpick_LDADD = @EFL_LIBS@
