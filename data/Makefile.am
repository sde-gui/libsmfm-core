NULL=

SUBDIRS=ui

man_MANS = libsmfm-pref-apps.1

mimedir = $(datadir)/mime/packages
mime_DATA = libsmfm.xml

libsmfmconfdir = $(sysconfdir)/xdg/libsmfm
libsmfmconf_DATA = \
	libsmfm.conf \
	pref-apps.conf \
	$(NULL)

desktopdir=$(datadir)/applications
desktop_in_files= \
	libsmfm-pref-apps.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

libsmfmdatafilesdir = $(datadir)/libsmfm
libsmfmdatafiles_DATA = \
	archivers.list \
	terminals.list \
	$(NULL)

EXTRA_DIST= \
	$(libsmfmconf_DATA) \
	$(mime_DATA) \
	$(libsmfmdatafiles_DATA) \
	$(desktop_in_files) \
	$(desktop_DATA) \
	$(NULL)

