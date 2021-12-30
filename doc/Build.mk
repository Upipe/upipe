MKDOC ?= mkdoc
DOT ?= dot

configs += doc
doc-command = $(MKDOC)
doc-disabled = y

distfiles = changelog.mkdoc \
            dependencies.dot \
            fdl-1.3.mkdoc \
            html_title.tmpl \
            intro.mkdoc \
            mkdoc.conf \
            overview.mkdoc \
            reference.mkdoc \
            rules.mkdoc \
            template.mkdoc \
            top.mkdoc \
            tutorials.mkdoc

data-targets = html
html-dest = $(htmldir)
html-deps = doc
html-gen = dependencies.png
html-dir = y

$(builddir)/html: $(addprefix $(srcdir)/,$(filter-out %.dot,$(distfiles)))
	$(call cmd,gen)$(MKDOC) --doc-path $(srcdir) \
	  -I $(top_srcdir)/include \
	  $$(cd "$(top_srcdir)/include" && ls */*.h)

$(builddir)/%.png: $(srcdir)/%.dot
	$(call cmd,gen)$(DOT) -Tpng $< > $@

.PHONY: doc
doc: $(builddir)/html
