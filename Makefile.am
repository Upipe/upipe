ACLOCAL_AMFLAGS = -I m4
SUBDIRS = lib include tests examples x86

if BUILD_LUAJIT
SUBDIRS += luajit
endif

dist_pkgdata_DATA = doc/fdl-1.3.mkdoc doc/intro.mkdoc doc/overview.mkdoc doc/template.mkdoc doc/top.mkdoc doc/html_title.tmpl doc/mkdoc.conf doc/rules.mkdoc doc/reference.mkdoc doc/tutorials.mkdoc doc/dependencies.dot

doc: doc/dependencies.png
	mkdoc -I include `cd include; ls */*.h`

doc/dependencies.png: doc/dependencies.dot
	dot -Tpng $< > $@

.PHONY: doc

check-whitespace:
	@check_attr() { \
	  git check-attr $$2 "$$1" | grep -q ": $$3$$"; \
	}; \
	pfx="^ *\([0-9]*\)	"; \
	git ls-files -- :*.[ch] | while read file; do \
	  check_attr "$$file" binary set && continue; \
	  check_attr "$$file" check-coding-style unset && continue; \
	  out=$$({ \
	    cat -n "$$file" | sed -n \
	      -e "s/$$pfx.* $$/\1: trailing whitespace/p" \
	      -e "s/$$pfx.*	.*/\1: invalid tab/p" \
	      -e "1s/$$pfx$$/\1: leading newline/p" \
	      -e "\$$s/$$pfx$$/\1: trailing newline/p"; \
	    { cat "$$file"; echo '#'; } | cat -n | sed -n \
	      -e "\$$s/$$pfx..*#$$/\1: missing newline/p"; \
	  } | while read -r line; do echo "$$file:$$line"; done); \
	  test -n "$$out" && echo "$$out" && ret=1; \
	  test -z "$$ret"; \
	done
