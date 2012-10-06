ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib include tests

dist_pkgdata_DATA = doc/fdl-1.3.mkdoc doc/install.mkdoc doc/intro.mkdoc doc/overview.mkdoc doc/template.mkdoc doc/top.mkdoc doc/html_title.tmpl doc/mkdoc.conf

doc: doc/dependancies.png
	mkdoc -I include `cd include; ls */*.h`

doc/dependancies.png: doc/dependancies.dot
	dot -Tpng $< > $@

.PHONY: doc
