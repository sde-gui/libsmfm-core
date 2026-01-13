include $(top_srcdir)/Makefile.decl

NULL=

# a little trick against gtk-doc is here
SUBDIRS_DOCS = docs

EXTRA_DIST_DOCS = \
	gtk-doc.make \
	docs/Makefile.am \
	docs/Makefile.in \
	docs/reference/Makefile.am \
	docs/reference/Makefile.in \
	docs/reference/libsmfm-core/Makefile.am \
	docs/reference/libsmfm-core/Makefile.in \
	docs/reference/libsmfm-core/libsmfm-core-docs.xml \
	docs/reference/libsmfm-core/libsmfm-core-sections.txt \
	docs/reference/libsmfm-core/version.xml.in

DIST_SUBDIRS = src po data
SUBDIRS = $(DIST_SUBDIRS) $(SUBDIRS_DOCS)

EXTRA_DIST += \
	libsmfm-core.pc.in \
	po/libfm.pot \
	$(NULL)

if ENABLE_GTK_DOC
DIST_SUBDIRS += $(SUBDIRS_DOCS)
else
EXTRA_DIST += $(EXTRA_DIST_DOCS)
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	libsmfm-core.pc \
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

cppcheck cppcheck-inconclusive:
	make -C src $@

ACLOCAL_AMFLAGS =  -I m4
