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
	mkdoc --doc-path $(srcdir)/doc -I $(srcdir)/include `cd $(srcdir)/include; ls */*.h`

doc/dependencies.png: $(srcdir)/doc/dependencies.dot
	$(MKDIR_P) doc/
	dot -Tpng $< > $@

.PHONY: doc

check-whitespace:
	@check_attr() { \
	  git check-attr $$2 "$$1" | grep -q ": $$3$$"; \
	}; \
	pfx="^ *\([0-9]*\)	"; \
	git ls-files -- :$(srcdir)/*.[ch] | while read file; do \
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

check-untracked:
	@if git ls-files $(srcdir) --others --exclude-standard | grep .; then exit 1; fi

check-headers:
	@if test -t 1; then \
	  red='[0;31m'; grn='[0;32m'; std='[m'; \
	fi; \
	for header in $(srcdir)/include/*/*.h; do \
	  h=$${header#$(srcdir)/include/}; \
	  if [ $$h != "upipe/config.h" ]; then \
	    guard=$$(echo _$${h}_ | tr [a-z] [A-Z] | tr '/.-' _); \
	    if ! grep -q $$guard $$header; then \
	      echo "$${red}FAIL$${std}: $$h: invalid guard"; ret=1; \
	    fi; \
	    if ! grep -q Copyright $$header; then \
	      echo "$${red}FAIL$${std}: $$h: missing copyright"; ret=1; \
	    fi; \
	  fi; \
	  if $(CC) -I$(srcdir)/include -I$(builddir)/include \
	    $(CPPFLAGS) $(CFLAGS) $(AM_CFLAGS) -Werror \
	    -include $$header -c -x c /dev/null -o /dev/null; \
	  then echo "$${grn}PASS$${std}: $$h"; \
	  else echo "$${red}FAIL$${std}: $$h"; ret=1; \
	  fi; \
	done; \
	test -z "$$ret"
