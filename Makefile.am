ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib include tests examples x86

if BUILD_LUAJIT
SUBDIRS += luajit
endif

EXTRA_DIST = doc/changelog.mkdoc \
             doc/dependencies.dot \
             doc/fdl-1.3.mkdoc \
             doc/html_title.tmpl \
             doc/intro.mkdoc \
             doc/mkdoc.conf \
             doc/overview.mkdoc \
             doc/reference.mkdoc \
             doc/rules.mkdoc \
             doc/template.mkdoc \
             doc/top.mkdoc \
             doc/tutorials.mkdoc

doc: doc/dependencies.png
	mkdoc -I include `cd include; ls */*.h`

doc/dependencies.png: doc/dependencies.dot
	dot -Tpng $< > $@

.PHONY: doc
