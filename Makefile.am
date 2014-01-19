ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib include tests examples

dist_pkgdata_DATA = doc/fdl-1.3.mkdoc doc/intro.mkdoc doc/overview.mkdoc doc/template.mkdoc doc/top.mkdoc doc/html_title.tmpl doc/mkdoc.conf doc/rules.mkdoc doc/reference.mkdoc doc/tutorials.mkdoc doc/dependancies.dot

doc: doc/dependancies.png
	mkdoc -I include `cd include; ls */*.h`

doc/dependancies.png: doc/dependancies.dot
	dot -Tpng $< > $@

.PHONY: doc
