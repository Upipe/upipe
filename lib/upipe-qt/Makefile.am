if HAVE_QTWEBKIT
lib_LTLIBRARIES = libupipe_qt.la
endif

BUILT_SOURCES = moc_thumbnail.cpp
CLEANFILES = moc_thumbnail.cpp

moc_thumbnail.cpp: thumbnail.h 
	$(MOC) -DQT_NO_DEBUG -DQT_WEBKIT_LIB -DQT_GUI_LIB -DQT_CORE_LIB $(QTWEBKIT_CFLAGS) -I. $< -o $@
libupipe_qt_la_SOURCES = upipe_qt_html.cpp thumbnail.cpp moc_thumbnail.cpp thumbnail.h
libupipe_qt_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include $(QTWEBKIT_CFLAGS) -I.
libupipe_qt_la_CFLAGS = -Wall 
libupipe_qt_la_CXXFLAGS = -Wall
libupipe_qt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(QTWEBKIT_LIBS)


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_qt.pc
