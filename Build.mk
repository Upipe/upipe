# --- general settings ---------------------------------------------------------

name         = upipe
description  = Upipe multimedia framework
version      = 1.0

cflags       = -O2 -g $(warn) $(warn_c)
cxxflags     = -O2 -g $(warn) $(warn_cxx)
cppflags     = -I$(top_builddir) -Iinclude -I$(top_srcdir)/include
ldflags      = $(if $(or $(have_apple),$(have_san)),,-Wl,--no-undefined)

warn         = -Wall \
               -Wextra \
               -Wno-unused-parameter \
               -Wno-sign-compare \
               -Wmissing-declarations \
               -Wwrite-strings \
               -Wredundant-decls \
               -Wundef \
               -Wmissing-include-dirs

warn_opt     = -Wstrict-prototypes \
               -Wmissing-prototypes \
               -Wold-style-definition \
               -Wduplicated-cond \
               -Wduplicated-branches \
               -Wlogical-op \
               -Wrestrict \
               -Wformat=2 \
               -Wno-vla-larger-than \
               -Wno-vla-cxx-extension

warn_c       = $(foreach w,$(warn_opt),$(call try_cc,$w))
warn_cxx     = $(foreach w,$(warn_opt),$(call try_cxx,$w))

distfiles    = AUTHORS \
               COPYING \
               COPYING.LGPLv2 \
               COPYING.MIT \
               INSTALL \
               README

subdirs      = lib examples tests x86 luajit doc

# --- config checks ------------------------------------------------------------

SED ?= sed

configs += atomic
atomic-assert = __ATOMIC_SEQ_CST

configs += eventfd
eventfd-includes = sys/eventfd.h
eventfd-functions = eventfd

configs += features.h
features.h-includes = features.h

configs += net/if.h
net/if.h-includes = net/if.h

configs += pic
pic-assert = __pic__

configs += pipe
pipe-includes = unistd.h
pipe-functions = pipe

configs += pthread
pthread-cppflags = -pthread
pthread-ldflags = -pthread

configs += semaphore
semaphore-includes = semaphore.h
semaphore-functions = sem_init

configs += unistd.h
unistd.h-includes = unistd.h

configs += writev
writev-includes = sys/uio.h
writev-functions = writev

# --- headers locations --------------------------------------------------------

includes-transform = :lib/%=include/%
includesubdir = $(notdir $(patsubst %/,%,$(dir $1)))

# --- config.h generation ------------------------------------------------------

genfiles = config.h include/upipe/config.h

config.h:
	$(call cmd,config.h)

# --- coding-style checks ------------------------------------------------------

check-whitespace:
	@check_attr() { \
	  git check-attr $$2 "$$1" | grep -q ": $$3$$"; \
	}; \
	pfx="^ *\([0-9]*\)	"; \
	git ls-files -- :$(top_srcdir)/*.[ch] | while read file; do \
	  check_attr "$$file" binary set && continue; \
	  check_attr "$$file" check-coding-style unset && continue; \
	  out=$$({ \
	    cat -n "$$file" | $(SED) -n \
	      -e "s/$$pfx.* $$/\1: trailing whitespace/p" \
	      -e "s/$$pfx.*	.*/\1: invalid tab/p" \
	      -e "1s/$$pfx$$/\1: leading newline/p" \
	      -e "\$$s/$$pfx$$/\1: trailing newline/p"; \
	    { cat "$$file"; echo '#'; } | cat -n | $(SED) -n \
	      -e "\$$s/$$pfx..*#$$/\1: missing newline/p"; \
	  } | while read -r line; do echo "$$file:$$line"; done); \
	  test -n "$$out" && echo "$$out" && ret=1; \
	  test -z "$$ret"; \
	done

check-untracked:
	@if git ls-files $(top_srcdir) --others --exclude-standard | grep .; then exit 1; fi

check-headers: include/upipe/config.h
	@for header in $(top_srcdir)/include/*/*.h; do \
	  h=$${header#$(top_srcdir)/include/}; \
	  if [ $$h != "upipe/config.h" ]; then \
	    guard=$$(echo _$${h}_ | tr [a-z] [A-Z] | tr '/.-' _); \
	    if ! grep -q $$guard $$header; then \
	      echo "$(c_red)FAIL$(c_end): $$h: invalid guard"; ret=1; \
	    fi; \
	    if ! grep -q Copyright $$header; then \
	      echo "$(c_red)FAIL$(c_end): $$h: missing copyright"; ret=1; \
	    fi; \
	  fi; \
	  if $(CC) -I$(top_srcdir)/include -Iinclude \
	    $(CPPFLAGS) $(CFLAGS) -Werror \
	    -include $$header -c -xc /dev/null -o /dev/null; \
	  then echo "$(c_green)PASS$(c_end): $$h"; \
	  else echo "$(c_red)FAIL$(c_end): $$h"; ret=1; \
	  fi; \
	done; \
	test -z "$$ret"

check-spelling:
	@codespell \
	  --skip=$(top_srcdir)/lib/upipe-blackmagic/include \
	  --skip=$(top_srcdir)/lib/upipe-modules/http-parser \
	  --ignore-words-list multicat,happend,inout,parms,hist \
	  --ignore-regex '"te\.' \
	  $(top_srcdir)/doc \
	  $(top_srcdir)/examples \
	  $(top_srcdir)/include \
	  $(top_srcdir)/lib \
	  $(top_srcdir)/luajit \
	  $(top_srcdir)/tests \
	  $(top_srcdir)/x86

check-tests:
	@cd $(top_srcdir)/tests; \
	for src in *.c; do \
	  if grep -q '^#undef NDEBUG' $$src; \
	  then echo "$(c_green)PASS$(c_end): $$src"; \
	  else echo "$(c_red)FAIL$(c_end): $$src"; ret=1; \
	  fi; \
	done; \
	test -z "$$ret"
