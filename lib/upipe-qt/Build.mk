lib-targets = libupipe_qt

libupipe_qt-desc = QtWebKit modules
libupipe_qt-includes = upipe_qt_html.h
libupipe_qt-src = upipe_qt_html.cpp thumbnail.cpp thumbnail.h
libupipe_qt-gen = moc_thumbnail.cpp
libupipe_qt-libs = libupipe QtWebKit

MOC = moc
MOCFLAGS = -DQT_NO_DEBUG -DQT_WEBKIT_LIB -DQT_GUI_LIB -DQT_CORE_LIB

configs += moc
moc-command = $(MOC)

cmd-moc = $(MOC) $(MOCFLAGS) $(cflags_QtWebKit) -I. $< -o $@
print-moc = MOC     $@

$(builddir)/moc_thumbnail.cpp: thumbnail.h
	$(call cmd,moc)
