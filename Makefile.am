AM_CPPFLAGS = -iquote $(top_builddir) -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

noinst_LTLIBRARIES = libfilelocktask.la

SOURCES = \
	FileLock.cxx \
	FileLock.h

libfilelocktask_la_SOURCES = ${SOURCES}
libfilelocktask_la_CXXFLAGS = @LIBCWD_R_FLAGS@
libfilelocktask_la_LIBADD = @LIBCWD_R_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
