# for use in libtool -version-info
ABI_VERSION=4:0:0

NULL=

# current dir needs to be built before tests
SUBDIRS= \
	$(ACTIONS_SUBDIR) \
	. \
	tests \
	$(NULL)

AM_CPPFLAGS = \
	-I$(srcdir) \
	-I$(srcdir)/base \
	-I$(srcdir)/actions \
	-I$(srcdir)/job \
	-DPACKAGE_DATA_DIR=\""$(datadir)/libsmfm"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)/libsmfm"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(NULL)

glib_compat_SOURCES= \
	glib-compat.c \
	glib-compat.h \
	$(NULL)

if ENABLE_UDISKS
udisks_SOURCES= \
	udisks/fm-udisks.c \
	udisks/fm-udisks.h \
	udisks/g-udisks-volume-monitor.c \
	udisks/g-udisks-volume-monitor.h \
	udisks/g-udisks-device.c \
	udisks/g-udisks-device.h \
	udisks/g-udisks-volume.c \
	udisks/g-udisks-volume.h \
	udisks/g-udisks-drive.c \
	udisks/g-udisks-drive.h \
	udisks/g-udisks-mount.c \
	udisks/g-udisks-mount.h \
	udisks/dbus-utils.c \
	udisks/dbus-utils.h \
	$(NULL)
else
udisks_SOURCES=
endif

base_SOURCES = \
	base/fm-config.c \
	base/fm-list.c \
	base/fm-path.c \
	base/fm-folder.c \
	base/fm-file-info.c \
	base/fm-file-info-deferred-load-worker.c \
	base/fm-highlighter.c \
	base/fm-highlighter.h \
	base/fm-mime-type.c \
	base/fm-utils.c \
	base/fm-file-launcher.c \
	base/fm-icon.c \
	base/fm-bookmarks.c \
	base/fm-nav-history.c \
	base/fm-monitor.c \
	base/fm-dummy-monitor.c \
	base/fm-archiver.c \
	base/fm-app-info.c \
	base/fm-thumbnailer.c \
	base/fm-thumbnail-loader.c \
	base/fm-file.c \
	base/fm-terminal.c \
	base/fm-templates.c \
	base/fm-marshal.c \
	vfs/fm-vfs-menu.c \
	vfs/fm-vfs-search.c \
	$(NULL)

job_SOURCES = \
	job/fm-job.c \
	job/fm-simple-job.c \
	job/fm-dir-list-job.c \
	job/fm-deep-count-job.c  \
	job/fm-file-ops-job.c \
	job/fm-file-info-job.c \
	job/fm-file-ops-job-xfer.c \
	job/fm-file-ops-job-delete.c \
	job/fm-file-ops-job-change-attr.c \
	$(NULL)

libsmfm_core_SOURCES = \
	fm.c \
	$(glib_compat_SOURCES) \
	$(base_SOURCES) \
	$(job_SOURCES) \
	$(udisks_SOURCES) \
	$(NULL)


libsmfmincludedir = $(includedir)/@PACKAGE@-@FMLIBVER@
libsmfminclude_HEADERS = \
	fm.h \
	base/fm-config.h \
	base/fm-list.h \
	base/fm-path.h \
	base/fm-folder.h \
	base/fm-file-info.h \
	base/fm-file-info-deferred-load-worker.h \
	base/fm-mime-type.h \
	base/fm-utils.h \
	base/fm-file-launcher.h \
	base/fm-icon.h \
	base/fm-bookmarks.h \
	base/fm-nav-history.h \
	base/fm-monitor.h \
	base/fm-dummy-monitor.h \
	base/fm-archiver.h \
	base/fm-app-info.h \
	base/fm-thumbnailer.h \
	base/fm-file.h \
	base/fm-terminal.h \
	base/fm-templates.h \
	base/fm-thumbnail-loader.h \
	base/fm-marshal.h \
	job/fm-job.h \
	job/fm-simple-job.h \
	job/fm-dir-list-job.h \
	job/fm-deep-count-job.h \
	job/fm-file-ops-job.h \
	job/fm-file-info-job.h \
	job/fm-file-ops-job-xfer.h \
	job/fm-file-ops-job-delete.h \
	job/fm-file-ops-job-change-attr.h \
	$(NULL)

pkginclude_HEADERS = \
	$(LIBSMFM_INCLUDES) \
	$(NULL)

lib_LTLIBRARIES = libsmfm-core.la
libsmfm_core_la_SOURCES = \
	$(libsmfm_core_SOURCES) \
	$(NULL)

libsmfm_core_la_CFLAGS = \
	$(GIO_CFLAGS) \
	$(MENU_CACHE_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(EXIF_CFLAGS) \
	-Werror-implicit-function-declaration \
	$(NULL)

libsmfm_core_la_LIBADD = \
	$(GIO_LIBS) \
	$(MENU_CACHE_LIBS) \
	$(DBUS_LIBS) \
	$(EXIF_LIBS) \
	$(INTLLIBS) \
	$(NULL)

libsmfm_core_la_DEPENDENCIES_EXTRA = \
	$(NULL)

libsmfm_core_la_LDFLAGS = \
	-no-undefined \
	-export-symbols-regex ^fm \
	-version-info $(ABI_VERSION) \
	$(NULL)


# GIO module implementing some extension points
# This only works for glib < 2.27
# Glib >= 2.27 uses x-scheme-handler.
if !HAVE_SCHEME_HANDLER

giomodules_LTLIBRARIES = libgiosmfm.la

libgiosmfm_la_SOURCES= \
	$(glib_compat_SOURCES) \
	gio/module.c \
	gio/fm-app-lookup.c \
	gio/fm-app-lookup.h \
	$(NULL)

libgiosmfm_la_CFLAGS = \
	$(GIO_CFLAGS) \
	-DGIO_MODULE_DIR=\"$(giomodulesdir)\"	\
	-DGVFS_LOCALEDIR=\""$(localedir)"\"	\
	-DG_DISABLE_DEPRECATED \
	$(NULL)

libgiosmfm_la_LDFLAGS = 	\
	-export_dynamic \
	-avoid-version \
	-module \
	-no-undefined \
	-export-symbols-regex '^g_io_module_(load|unload|query)'
	$(NULL)

libgiosmfm_la_LIBADD = \
	$(GIO_LIBS) \
	$(INTLLIBS) \
	$(NULL)

endif


# GObject marshallers
base/fm-marshal.h: base/fm-marshal.list
	glib-genmarshal --header --prefix=fm_marshal $? > $@.tmp && mv $@.tmp $@

base/fm-marshal.c: base/fm-marshal.list
	glib-genmarshal --body --prefix=fm_marshal $? > $@.tmp && mv $@.tmp $@

# Workarounds to force the build
BUILT_SOURCES= \
	base/fm-marshal.h \
	base/fm-marshal.c \
	$(NULL)

EXTRA_DIST = \
	base/fm-marshal.list \
	udisks/gen-binding \
	udisks/udisks.h \
	udisks/udisks-device.h \
	vfs/fm-vfs-menu.h \
	vfs/fm-vfs-search.h \
	$(NULL)

CLEANFILES = \
	base/fm-marshal.h \
	base/fm-marshal.c \
	$(NULL)

# Little program to optimize size of xml files
noinst_PROGRAMS=xml-purge
xml_purge_SOURCES=xml-purge.c
xml_purge_CFLAGS=$(GIO_CFLAGS)
xml_purge_LDADD=$(GIO_LIBS)

install-data-local:
	@if test -e "$(DESTDIR)$(includedir)/@PACKAGE@"; then \
		echo rm -rf "$(DESTDIR)$(includedir)/@PACKAGE@"; \
		rm -rf "$(DESTDIR)$(includedir)/@PACKAGE@"; \
	fi
	test -z "$(includedir)" || $(MKDIR_P) "$(DESTDIR)$(includedir)"
	@LN_S@ @PACKAGE@-@FMLIBVER@ "$(DESTDIR)$(includedir)/@PACKAGE@"

uninstall-local:
	@if test -L "$(DESTDIR)$(includedir)/@PACKAGE@"; then \
		echo rm -f "$(DESTDIR)$(includedir)/@PACKAGE@"; \
		rm -f "$(DESTDIR)$(includedir)/@PACKAGE@"; \
	fi
	-rmdir "$(DESTDIR)$(libsmfmincludedir)"

install-data-local: install-pkgincludeHEADERS
