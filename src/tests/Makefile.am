include $(top_srcdir)/Makefile.decl

NULL=

AM_CPPFLAGS = \
	-I$(srcdir)/.. \
	-I$(srcdir)/../basekit \
	-I$(srcdir)/../base \
	-I$(srcdir)/../extra \
	-I$(srcdir)/../job \
	-I$(abs_builddir)/.. \
	-I$(abs_builddir)/../base \
	$(GIO_CFLAGS) \
	$(MENU_CACHE_CFLAGS) \
	$(DBUS_CFLAGS) \
	-Werror-implicit-function-declaration \
	$(NULL)

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += fm-path
fm_path_SOURCES = test-fm-path.c
fm_path_LDADD= \
	../libsmfm-core.la \
	$(GIO_LIBS) \
	$(NULL)
