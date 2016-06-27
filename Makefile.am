ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib include tests examples

dist_pkgdata_DATA = doc/fdl-1.3.mkdoc doc/intro.mkdoc doc/overview.mkdoc doc/template.mkdoc doc/top.mkdoc doc/html_title.tmpl doc/mkdoc.conf doc/rules.mkdoc doc/reference.mkdoc doc/tutorials.mkdoc doc/dependencies.dot

doc: doc/dependencies.png
	mkdoc -I include `cd include; ls */*.h`

doc/dependencies.png: doc/dependencies.dot
	dot -Tpng $< > $@

.PHONY: doc
