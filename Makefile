# ------------------------------------------------------------------------------
#  Generic build system for GNU Make
#  Copyright (C) 2022-2026 Clément Vasseur <clement.vasseur@gmail.com>
#  SPDX-License-Identifier: MIT
# ------------------------------------------------------------------------------

MAKEFLAGS += -rR

# --- build environment --------------------------------------------------------

CC            = $(CROSS)gcc
CXX           = $(CROSS)g++
CPP           = $(CC) -E
AR            = $(CROSS)ar
PKG_CONFIG    = $(CROSS)pkg-config
INSTALL       = install
MKDIR         = mkdir
RMDIR         = rmdir
NASM          = nasm
TAR           = tar
RM            = rm -f
CP            = cp -d
FIND          = find
CHMOD         = chmod

prefix        = /usr/local
exec_prefix   = $(prefix)
bindir        = $(exec_prefix)/bin
sbindir       = $(exec_prefix)/sbin
libdir        = $(exec_prefix)/lib
libexecdir    = $(exec_prefix)/libexec
datarootdir   = $(prefix)/share
datadir       = $(datarootdir)/$(name)
docdir        = $(datarootdir)/doc/$(name)
htmldir       = $(docdir)
includedir    = $(prefix)/include
pkgconfigdir  = $(libdir)/pkgconfig
distdir       = $(name)-$(version)

# --- command line flags -------------------------------------------------------

P =
V =
PROGRESS = $P
VERBOSE = $V
VERBOSE_CONFIG =
VERBOSE_TESTS =
TEST_SUITE_LOG = $(_sub)test-suite.log
BUILD_COLORS ?= auto

# --- time profiler ------------------------------------------------------------

ifdef PROFILE
  $(shell $(RM) build-times.log)
  _shell := $(SHELL) $(.SHELLFLAGS)
  SHELL = time
  .SHELLFLAGS = --quiet -ao build-times.log -f %e\ $@ $(_shell)
  print-mode = date
  cmd-date = @trap 'echo $$_d-$$(date +%s.%N) $@' EXIT; _d=$$(date +%s.%N);
endif

# --- load config file ---------------------------------------------------------

define \n


endef

ifeq ($(shell command -v $(PKG_CONFIG)),)
  PKG_CONFIG = pkg-config
endif

config := $(if $(filter configure,$(MAKECMDGOALS)),,$(wildcard config.mk))

ifdef config
  ifeq ($(wildcard $(config)),)
    $(error config file '$(config)' not found)))
  endif
  $(eval $(or $(file < $(config)),$(subst :,$(\n), \
    $(shell while read c; do printf %s: "$$c"; done < $(config)))))
endif

# --- subdirs ------------------------------------------------------------------

.SECONDEXPANSION:

top_srcdir := $(patsubst %/,%,$(dir $(firstword $(MAKEFILE_LIST))))
top_builddir = .

ifneq ($(top_srcdir),$(top_builddir))
  _oot = y
endif

ifdef _oot
  $(foreach e,h c cpp asm sh in,$(eval vpath %.$e $(top_srcdir)))
endif

_F = distfiles depfiles cleanfiles cleandirs genfiles
_T = $(addsuffix -targets,bin lib data dist noinst test) tests
_attrs_p = includes gen-includes src gen
_attrs = cppflags cflags cxxflags nasmflags ldflags ldlibs libs opt-libs \
         deps desc dest opt args version so-version dir

define _redef
  _$1 += $3$2
  $(foreach a,$(_attrs),$$(eval $3$2-$a = $$(value $2-$a)))
  $(foreach a,$(_attrs_p),$$(eval $3$2-$a = $$$$(addprefix $3,$$(value $2-$a))))
endef

define _subdir
  subdirs :=
  builddir := $(patsubst %/,%,$1)
  $$(foreach v,$(_T) $(_F),$$(eval $$v :=))
  include $(top_srcdir)/$1Build.mk
  $$(foreach T,$(_T),$$(foreach t,$$($$T),$$(eval $$(call _redef,$$T,$$t,$1))))
  $$(foreach F,$(_F),$$(eval _$$F += $$$$(addprefix $1,$$(value $$F))))
  $$(foreach d,$$(subdirs),$$(eval $$(call _subdir,$1$$d/)))
endef

srcdir = $(top_srcdir)/$(builddir)
$(eval $(call _subdir))

_test-targets += $(foreach t,$(_tests),$(if $($t-src),$t))
_inst-targets := $(_bin-targets) $(_lib-targets) $(_data-targets) $(_dist-targets)
_bin-targets  += $(_noinst-targets) $(_test-targets)
_targets      := $(_inst-targets) $(_noinst-targets) $(_test-targets)

builddir = $(patsubst %/,%,$(dir $(_target)))
name ?= $(firstword $(_targets))
version ?= 1.0

# --- pkg-config ---------------------------------------------------------------

_pkg_reqs = $(filter-out $(configs),$($1-libs) $($1-opt-libs))
_pkg_libs = $($1-ldlibs) \
  $(foreach c,$(filter $(configs),$($1-libs) $($1-opt-libs)),\
  $$(ldflags_$c) $$(ldlibs_$c))

_pkgs := $(sort $(filter-out $(notdir $(_targets)),\
  $(foreach t,$(_targets),$(call _pkg_reqs,$t))) \
  $(foreach c,$(configs),$($c-libs)))

define _pkg-config
  ifneq ($$(filter undefined environment,$$(origin have_$1)),)
    have_$1 := $$(shell $(PKG_CONFIG) --exists $1 && echo y)
    ifeq ($$(have_$1),y)
      version_$1  := $$(strip $$(shell $(PKG_CONFIG) --modversion $1))
      cppflags_$1 := $$(strip $$(shell $(PKG_CONFIG) --cflags $1))
      ldlibs_$1   := $$(strip $$(shell $(PKG_CONFIG) --libs $1))
      prefix_$1   := $$(strip $$(shell $(PKG_CONFIG) --variable=prefix $1))
    endif
  endif
endef

_help_goals = help help-config help-pkgs help-config-list
ifeq ($(if $(MAKECMDGOALS),$(filter-out $(_help_goals),$(MAKECMDGOALS)),y),)
  _skip_config = y
endif

ifndef _skip_config
  $(foreach p,$(_pkgs),$(eval $(call _pkg-config,$p)))
endif

$(foreach t,$(_lib-targets),\
  $(eval $t-pc-reqs = $(sort $(call _pkg_reqs,$t)))\
  $(eval $t-pc-libs = $(sort $(call _pkg_libs,$t))))

_c = ,
_desc := $(if $(description),$(description)$(_c) )

$(_lib-targets:=.pc): %.pc:
	$(call cmd,gen){ \
	  echo "prefix=$(prefix)"; \
	  echo "exec_prefix=$(exec_prefix)"; \
	  echo "libdir=$(libdir)"; \
	  echo "includedir=$(includedir)"; \
	  echo "Name: $(*F)"; \
	  echo "Description: $(_desc)$(or $($*-desc),$(*F))"; \
	  echo "Version: $(or $($*-version),$(version))"; \
	  echo "Cflags: -I\$${includedir}"; \
	  echo "Libs: -L\$${libdir} -l$(*F:lib%=%)"; \
	  $(if $($*-pc-libs),echo "Libs.private: $($*-pc-libs)";) \
	  $(if $($*-pc-reqs),echo "Require.private: $($*-pc-reqs)";) \
	} > $@

# --- config checks ------------------------------------------------------------

configs += apple static static-pic $(if $(strip $(_lib-targets)),shared)
shared-ldflags = -shared
apple-assert = __APPLE__
static-pic-disabled = y

define _have_target
  have_$(notdir $1) = $$(if $$(strip $$(foreach l,$$($1-libs) $$($1-deps),\
    $$(if $$(have_$$l),,n))),,y)
endef

_uniq = $(if $1,$(firstword $1) $(call $0,$(filter-out $(firstword $1),$1)))

define _have_lib
  _rpath_$(notdir $1) = $(if $(have_apple),,-Wl,-rpath-link=$(dir $1)) \
    $$(sort $$(foreach l,$$($1-libs) $$($1-opt-libs),$$(_rpath_$$l)))
  _cxx_$(notdir $1) = $$(strip $$(filter %.cpp,$$($1-src)) \
    $(if $(have_shared),, \
    $$(foreach l,$$($1-libs) $$($1-opt-libs),$$(_cxx_$$l))))
  ldflags_$(notdir $1) = $$(call _uniq,-L$(patsubst %/,%,$(dir $1)) \
    $(if $(have_shared),$$(_rpath_$(notdir $1)), \
    $$(foreach l,$$($1-libs) $$($1-opt-libs),$$(ldflags_$$l)) $$($1-ldflags)))
  ldlibs_$(notdir $1) = $$(call _uniq,-l$(patsubst lib%,%,$(notdir $1)) \
    $(if $(have_shared),, \
    $$(foreach l,$$($1-libs) $$($1-opt-libs),$$(ldlibs_$$l)) $$($1-ldlibs)))
endef

$(foreach t,$(_targets),$(eval $(call _have_target,$t)))
$(foreach t,$(_lib-targets),$(eval $(call _have_lib,$t)))

ifneq ($(filter configure,$(MAKECMDGOALS)),)
  _configure = y
  $(shell echo 'Config log for $(name) $(version)' > config.log)
  _c_exec = exec 2>>config.log;
  VERBOSE_CONFIG = 1
endif

ifdef VERBOSE_CONFIG
  _c_title = $(_c_exec) { echo; echo checking $c; } >&2; set -x;
else
  _c_hide_err = 2>/dev/null
endif

_c_assert = $(if $($1-assert),_Static_assert($($1-assert), \"$($1-assert)\"); )
_c_syms = return $($1-functions:%=(void)%,) 0;

ifndef _skip_config
$(foreach c,$(sort $(configs)), \
  $(if $(or $(_configure),$(filter-out file,$(origin have_$c))), \
    $(eval have_$c = $(if $($c-disabled),,y)) \
    $(if $(have_$c), \
      $(eval override have_$c := \
        $(if $($c-command), \
          $(shell $(_c_title) \
            command -v $($c-command) $(_c_hide_err) >&2 && echo y), \
        $(if $(or $($c-assert),$($c-includes),$($c-functions),$($c-ldlibs)), \
          $(shell $(_c_title) \
            $(foreach p,$($c-libs),$(if $(have_$p),true,false) $p && ) \
            echo "int main(void) { $(call _c_assert,$c)$(call _c_syms,$c) }" | \
            $(CC) $($c-includes:%=-include %) \
            $(CPPFLAGS) $($c-cppflags) $(foreach p,$($c-libs),$(cppflags_$p)) \
            $(CFLAGS) $(LDFLAGS) $($c-ldflags) \
            $(LDLIBS) $($c-ldlibs) $(foreach p,$($c-libs),$(ldlibs_$p)) \
            -o config.out -g0 -xc - $(_c_hide_err) && echo y; \
            { set +x; } 2>&-; $(RM) config.out), \
        $(if $($c-libs), \
          $(if $(strip $(foreach l,$($c-libs),$(if $(have_$l),,n))),,y),y)))))))
endif

$(foreach f,cppflags ldflags ldlibs,\
  $(foreach c,$(configs),$(eval $f_$c := $(if $(have_$c),$($f_$c) $($c-$f)))))

_disabled := $(sort $(foreach t,$(_targets),\
  $(foreach l,$($t-libs) $($t-deps),$(if $(have_$l),,$t))))

_try = $(if $(or $(filter .,$(print-mode)),\
  $(filter file,$(origin _try/$2/$(subst =,_,$3)))),,\
  $(eval _try/$2/$(subst =,_,$3) = \
    $(if $(shell $1 -E -Werror $(patsubst -Wno-%,-W%,$3) -x$2 \
    -c /dev/null -o /dev/null 2>/dev/null && echo y),$3)))\
  $(_try/$2/$(subst =,_,$3))

try_cc = $(call _try,$(CC),c,$1)
try_cxx = $(call _try,$(CXX),c++,$1)

# --- targets ------------------------------------------------------------------

.SUFFIXES:
.SECONDARY:
.DELETE_ON_ERROR:
.DEFAULT_GOAL = all

$(foreach t,$(_targets),\
  $(eval $t-opt-libs = $(filter-out $(notdir $(_disabled)),$($t-opt-libs))))

_so = $(if $(have_apple),dylib,so)
_so_v = $(if $(filter dynamic,$($1-opt)),,$(or $($1-so-version),0.0.0))
_so_maj = $(if $(_so_ver),.$(word 1,$(subst ., ,$(_so_ver))))
_lib-ext = $(foreach t,$1,$(if $(have_shared),$t.$(_so)) \
  $(if $(filter dynamic,$($t-opt)),,\
    $(if $(have_static-pic),$t_pic.a) $(if $(have_static),$t.a) $t.pc))
_lib-lnk = $(foreach t,$1,$(foreach _so_ver,$(call _so_v,$t),\
  $t.$(_so).$(_so_ver) $t.$(_so)$(_so_maj)))
_libs-targets := $(filter-out %.pc,$(call _lib-ext,$(_lib-targets)))
_pc-targets = $(filter %.pc,$(call _lib-ext,$(_lib-targets)))
_tsubst = $(foreach e,.c .cpp .asm,$(patsubst %$e,%$2,$(filter %$e,$($1-src))))
_o = $(foreach t,$1,$(call _tsubst,$t,$2))
_out_o := $(call _o,$(_targets),.o) $(call _o,$(_lib-targets),-pic.o)
_out_l := $(if $(have_shared),$(call _lib-lnk,$(_lib-targets)))
_out_t := $(foreach T,bin libs pc test data,$(_$T-targets))
_out_c := $(_tests:=.log) $(_tests:=.trs) $(TEST_SUITE_LOG)
_out_g := $(foreach t,$(_targets),\
  $($t-gen) $($t-gen-includes$(includes-transform))) $(_genfiles)
_out := $(sort $(_out_o) $(_out_t) $(_out_c) $(_out_g))
_depfiles += $(_out_o:.o=.d)
_cleanfiles += $(_out) $(_out_l) $(_depfiles)
_cleandirs += $(foreach t,$(_data-targets),$(if $($t-dir),$t))
_dirs := $(filter-out ./,$(sort $(dir $(_out))))
_cxx = $(strip $(filter %.cpp,$($(_target)-src)) \
  $(foreach l,$($(_target)-libs) $($(_target)-opt-libs),$(_cxx_$l)))

$(foreach T,bin lib data,$(foreach t,$(_$T-targets),\
  $(eval $t install-$T-$t uninstall-$T-$t: _target = $t)))
$(foreach t,$(_dist-targets),\
  $(eval $t install-data-$t uninstall-data-$t: _target = $t))
$(foreach t,$(_lib-targets),\
  $(eval install-inc-$t uninstall-inc-$t: _target = $t))
$(foreach t,$(_lib-targets),$(eval $(call _lib-ext,$t): _target = $t))
$(foreach t,$(_lib-targets),$(eval $t: $(call _lib-ext,$t)))
$(foreach t,$(_targets),$(if $($t-gen), \
  $(eval $(if $($t-src),$(call _tsubst,$t,.o),$t): $($t-gen))))
$(foreach t,$(_lib-targets),$(if $($t-gen), \
  $(eval $(call _tsubst,$t,.o): $($t-gen)) \
  $(eval $(call _tsubst,$t,-pic.o): $($t-gen))))
$(foreach t,$(_lib-targets),$(if $($t-gen-includes), \
  $(eval $(call _tsubst,$t,.o): $($t-gen-includes$(includes-transform))) \
  $(eval $(call _tsubst,$t,-pic.o): $($t-gen-includes$(includes-transform)))))
$(if $(_genfiles),$(_out_o): $(_genfiles))

# --- flags --------------------------------------------------------------------

_flags = _$1 = \
  $$(_$2) \
  $$($2) \
  $$($$(_target)-$2) \
  $$(foreach l,$$($$(_target)-libs),$$($2_$$l)) \
  $$(foreach l,$$($$(_target)-opt-libs),$$($2_$$l)) \
  $$($$*-$2) \
  $$($$(_target)-$$(notdir $$*)-$2) \
  $$($1)

$(eval $(call _flags,CPPFLAGS,cppflags))
$(eval $(call _flags,CFLAGS,cflags))
$(eval $(call _flags,CXXFLAGS,cxxflags))
$(eval $(call _flags,NASMFLAGS,nasmflags))
$(eval $(call _flags,LDFLAGS,ldflags))
$(eval $(call _flags,LDLIBS,ldlibs))

# --- commands -----------------------------------------------------------------

_dest-bin       = $(DESTDIR)$(or $($(_target)-dest),$(bindir))
_dest-lib       = $(DESTDIR)$(or $($(_target)-dest),$(libdir))
_dest-data      = $(DESTDIR)$(or $($(_target)-dest),$(datadir))
_dest-inc       = $(DESTDIR)$(includedir)/$(call includesubdir,$(_target))
_dest-pc        = $(DESTDIR)$(pkgconfigdir)

cmd-cc          = $(CC) $(_CPPFLAGS) $(_CFLAGS) -o $@ -c $<
cmd-cxx         = $(CXX) $(_CPPFLAGS) $(_CXXFLAGS) -o $@ -c $<
cmd-nasm        = $(NASM) $(_NASMFLAGS) -o $@ $<
cmd-ld          = $(if $(_cxx),$(CXX),$(CC)) $(_LDFLAGS) -o $@$(_so_ver) \
                  $(filter %.o,$^) $(filter %.a,$^) $(_LDLIBS) \
                  $(if $(_so_ver), \
                  && ln -sf $(notdir $@$(_so_ver)) $@$(_so_maj) \
                  && ln -sf $(notdir $@$(_so_ver)) $@)
cmd-ar          = $(AR) crs $@ $^
cmd-inst-dir    = $(INSTALL) -d $@
cmd-inst-bin    = $(INSTALL) -m 755 $< $(_dest-bin)
cmd-inst-lib    = $(if $(_so_ver),$(CP) $(call _lib-lnk,$(_target)),\
                  $(INSTALL) -m 644) $< $(_dest-lib)
cmd-inst-data   = $(if $($(_target)-dir),$(CP) -r,\
                  $(INSTALL) -m 644) $< $(_dest-data)
cmd-inst-inc    = $(INSTALL) -m 644 $^ $(_dest-inc)
cmd-inst-pc     = $(INSTALL) -m 644 $< $(_dest-pc)
cmd-uninst-bin  = $(RM) $(_dest-bin)/$(<F)
cmd-uninst-lib  = $(RM) $(addprefix $(_dest-lib)/,\
                  $(if $(_so_ver),$(notdir $(call _lib-lnk,$(_target)))) $(<F))
cmd-uninst-data = $(RM) $(if $($(_target)-dir),-r )$(_dest-data)/$(<F)
cmd-uninst-inc  = $(RM) $(foreach f,$^,$(_dest-inc)/$(notdir $f))
cmd-uninst-pc   = $(RM) $(_dest-pc)/$(<F)
cmd-mkdir       = $(MKDIR) -p $@
cmd-clean-f     = $(if $(_cleandirs),$(RM) -r $(sort $(_cleandirs)) && )\
                  $(RM) $(sort $(_cleanfiles))
cmd-clean-c     = $(RM) $(sort $(_distcleanfiles))
cmd-clean-d     = $(RMDIR) -p $(_dirs) 2>/dev/null;:
cmd-tar         = $(TAR) caf $@ -C $(top_srcdir) $(_TARFLAGS) $(sort \
                  $(patsubst $(top_srcdir)/%,%,$(filter-out /%,$^)) \
                  $(patsubst $(abspath $(top_srcdir))/%,%,$(filter /%,$^)))

# --- pretty print -------------------------------------------------------------

print-cc          = CC $(if $(filter -fPIC,$(_CFLAGS)),[P],   )   $@
print-cxx         = CXX $(if $(filter -fPIC,$(_CXXFLAGS)),[P],   )  $@
print-nasm        = ASM      $@
print-ld          = LINK $(if $(_cxx),[X],   ) $@
print-ar          = AR       $@
print-inst-dir    = INST [D] $(@:$(DESTDIR)%=%)
print-inst-bin    = INST     $<
print-inst-lib    = INST     $<
print-inst-data   = INST     $<
print-inst-inc    = INST     $(dir $(_target$(includes-transform)))*.h
print-inst-pc     = INST     $<
print-uninst-bin  = UNINST   $*
print-uninst-lib  = UNINST   $*
print-uninst-data = UNINST   $*
print-uninst-inc  = UNINST   $*
print-uninst-pc   = UNINST   $*
print-mkdir       = MKDIR    $@
print-clean-f     = CLEAN    files
print-clean-c     = CLEAN    config
print-clean-d     = CLEAN    directories
print-tar         = TAR      $@
print-gen         = GEN      $@

# --- rules --------------------------------------------------------------------

print-mode ?= $(if $(if $V,$(filter-out 0,$V),$(VERBOSE)),,quiet)

_npd = --no-print-directory
_chop = $(wordlist 2,$(words $1),_ $1)
__dsplit = $(if $2,$(subst $(lastword $2),$(lastword $2) ,$(call $0,$1,$(call _chop,$2))),$1)
_dsplit = $(call __dsplit,$1,0 1 2 3 4 5 6 7 8 9)
__pad = $(if $2,$(call $0,$(call _chop,$1),$(call _chop,$2))$(if $1,$(lastword $1), ))
_pad = $(call __pad,$(call _dsplit,$1),$(call _dsplit,$2))

ifneq ($(PROGRESS),)
  _f := -f $(firstword $(MAKEFILE_LIST))
  _pt := $(words $(shell $(MAKE) $(_npd) $(_f) $(MAKECMDGOALS) P= PROGRESS= print-mode=.))
  _p = $(eval _pn += .)  [$(call _pad,$(words $(_pn)),$(_pt))/$(_pt)]
endif

cmd-. = @$(info .) exit 0;
cmd-quiet = @$(info $(_p)  $(or $(print-$1),$(print-gen)))
cmd = $(cmd-$(print-mode))$(strip $(cmd-$1))

.PHONY: all all-tests clean distclean $(_lib-targets)
all: $(filter $(_sub)%,$(filter-out $(_disabled) $(_test-targets),$(_targets)))
all-tests: all $(filter $(_sub)%,$(filter-out $(_disabled),$(_test-targets)))

clean: override _cleanfiles := $(filter $(_sub)%,$(_cleanfiles))
clean: override _cleandirs := $(filter $(_sub)%,$(_cleandirs))
clean:
	$(call cmd,clean-f)

distclean: clean
ifndef _sub
	$(call cmd,clean-c)
	$(if $(_oot),$(call cmd,clean-d))
endif

_inst-enabled = $(filter $(_sub)%,$(filter $(_inst-targets),$(filter-out $(_disabled),$1)))

define _inst-rule
  .PHONY: $(_$2-targets:%=$1-$3-%)
  $(_$2-targets:%=$1-$3-%): $1-$3-%: $4 | $$$$(_dest-$3)
endef

define _install
  .PHONY: $1 $1-bin $1-lib $1-data
  $1: $1-bin $1-lib $1-data
  $1-bin: $(addprefix $1-bin-,$(call _inst-enabled,$(_bin-targets)))
  $1-lib: $(addprefix $1-lib-,$(call _inst-enabled,$(_lib-targets)))
  $1-data: $(addprefix $1-data-,$(call _inst-enabled,$(_data-targets) $(_dist-targets)))

  $(call _inst-rule,$1,bin,bin,%); $$(call cmd,$2-bin)
  $(call _inst-rule,$1,libs,lib,%); $$(call cmd,$2-lib)
  $(call _inst-rule,$1,data,data,%); $$(call cmd,$2-data)
  $(call _inst-rule,$1,dist,data,%); $$(call cmd,$2-data)
  $(call _inst-rule,$1,lib,pc,%.pc); $$(call cmd,$2-pc)
  $(call _inst-rule,$1,lib,inc,\
    $$$$(%-includes$$$$(includes-transform)) \
    $$$$(%-gen-includes$$$$(includes-transform)))
	$$(if $$^,$$(call cmd,$2-inc))
  $(_lib-targets:%=$1-lib-%): $1-lib-%: \
    $$$$(if $$$$(have_shared),$1-lib-%.$(_so)) \
    $$$$(if $$$$(filter dynamic,$$$$(%-opt)),,\
      $$$$(if $$$$(have_static),$1-lib-%.a) \
      $$$$(if $$$$(have_static-pic),$1-lib-%_pic.a) \
      $1-pc-% $1-inc-%)
endef

$(eval $(call _install,install,inst))
$(eval $(call _install,uninstall,uninst))

_install-dirs = $(sort \
  $(foreach _target,$(_bin-targets),$(_dest-bin)) \
  $(foreach _target,$(_lib-targets),$(_dest-lib) $(_dest-inc)) \
  $(foreach _target,$(_data-targets) $(_dist-targets),$(_dest-data)) \
  $(_dest-pc))

-Wl = -Wl,$1$(if $2,$(_c)$2)

%-pic.o: override CFLAGS += -fPIC
%-pic.o: override CXXFLAGS += -fPIC
%-pic.o: override NASMFLAGS += -DPIC
%.so: override _so_ver = $(patsubst %,.%,$(call _so_v,$(_target)))
%.so: override LDFLAGS += -shared $(if $(_so_maj),$(call -Wl,-soname=$(@F)$(_so_maj)))
%.dylib: override LDFLAGS += -dynamiclib -install_name $(@F)

%-pic.o: %.c;      $(call cmd,cc)
%.o: %.c;          $(call cmd,cc)
%-pic.o: %.cpp;    $(call cmd,cxx)
%.o: %.cpp;        $(call cmd,cxx)
%.o: %.asm;        $(call cmd,nasm)
%-pic.o: %.asm;    $(call cmd,nasm)
$(_dirs):;         $(call cmd,mkdir)
$(_install-dirs):; $(call cmd,inst-dir)

dist: $(distdir).tar.xz

$(foreach c,$(_pkgs) $(configs),$(eval $(distdir).tar.%: override have_$c=y))

_gnu-tar = $(filter GNU,$(shell $(TAR) --version))
_tar-transform = $(if $(_gnu-tar),--transform 's$1',-s '$1')

$(distdir).tar.%: override _TARFLAGS = $(call _tar-transform,|^|$(distdir)/|)
$(distdir).tar.%: \
    $(abspath $(top_srcdir)/configure) \
    $(filter-out $(_depfiles),$(MAKEFILE_LIST)) \
    $(addprefix $(top_srcdir)/,$(_distfiles)) \
    $(filter-out $(_test-targets),$(_tests)) \
    $(_dist-targets) \
    $$(foreach t,$(_targets),$$(filter-out $$($$t-gen), \
      $$($$t-src) $$($$t-includes$$(includes-transform))))
	$(call cmd,tar)

# --- json compilation database ------------------------------------------------

_json = { "directory": "$(CURDIR)", \
          "output": "$@", \
          "file": "$<", \
          "command": "$(strip $(filter-out -MD -MP,$(cmd-$1)))" }

cmd-db = @$(if $(filter %.o,$@),$(info $(_json),)) exit 0;

.PHONY: compile_commands.json
compile_commands.json:
	@db=$$($(MAKE) $(_npd) -B all-tests print-mode=db); echo "[$${db%?}]" > $@

# --- colors -------------------------------------------------------------------

ifeq ($(BUILD_COLORS),always)
  _colors = y
endif

ifeq ($(BUILD_COLORS),auto)
  _colors = $(MAKE_TERMOUT)
endif

ifneq ($(_colors),)
  c_red   = [31m
  c_green = [32m
  c_gray  = [90m
  c_end   = [m
endif

# --- test suite ---------------------------------------------------------------

.PHONY: check distcheck

_tn = $(t:check-%=%)
_read = $(or $(file <$1),$(shell cat '$1'))
_rel = $(subst $() ,/,$(foreach d,$(subst /, ,$1),$(if $(filter .,$d),.,..)))/
_<P = $(if $(filter-out /%,$<),$(call _rel,$(*D)))$<
_ext = $(subst .,-,$(suffix $*))
_log-compiler = $(or $(log-compiler$(_ext)),$(log-compiler))
_log-flags = $(if $(log-compiler$(_ext)),$(log-flags$(_ext)),$(log-flags))
_log-env = $(if $(log-compiler$(_ext)),$(log-env$(_ext)),$(log-env))

_LD_LIBRARY_PATH = $(subst $() ,:,$(foreach d,\
  $(sort $(dir $(_lib-targets))),$(call _rel,$(*D))$d))

_DY = $(if $(have_apple),DY)

$(_tests:%=check-%): check-%: % \
  $(filter-out $(_disabled),$(_targets)) | $(dir $(_tests))
	@export $(_DY)LD_LIBRARY_PATH="$(_LD_LIBRARY_PATH):$$$(_DY)LD_LIBRARY_PATH"; \
	 $(if $(strip $(_log-env)),export $(_log-env);) \
	cd $(*D) && if $(_log-compiler) $(_log-flags) \
	  $(_<P) $($*-args) >$(*F).log 2>&1; \
	  then status=$$?; res=PASS; color=$(c_green); \
	  else status=$$?; res=FAIL; color=$(c_red); \
	fi; \
	echo $$res '$(*F)$(if $($*-args), $($*-args))' \
	  "(exit status: $$status)" >>$(*F).log; \
	echo $$res >$(*F).trs; \
	echo "$$color$$res$(c_end): $(*F)"

_disabled-tests := $(sort $(_disabled) $(foreach t,$(_tests),\
  $(foreach d,$($t-deps),$(if $(have_$d),,$t))))

_br = ============================================================================
_sp = $(shell echo '$1' | tr [:print:] =)

define _tests_summary
  echo "# TOTAL: $(words $1)"; \
  echo "# PASS:  $(words $(filter PASS,$1))"; \
  echo "# FAIL:  $(words $(filter FAIL,$1))";
endef

_enabled-tests = $(filter $(_sub)%,$(filter-out $(_disabled-tests),$(_tests)))

check: $(addprefix check-,$(_enabled-tests))
	$(eval _res := $(or \
	  $(strip $(foreach t,$^,$(file <$(_tn).trs))), \
	  $(shell cat $(^:check-%=%.trs))))
	$(eval _ts_title = $(name) $(version): $(TEST_SUITE_LOG))
	@{ echo '===$(call _sp,$(_ts_title))==='; \
	   echo '   $(_ts_title)'; \
	   echo '===$(call _sp,$(_ts_title))==='; \
	   echo; \
	   $(call _tests_summary,$(_res)) \
	   $(foreach t,$^,$(if $(filter FAIL,$(call _read,$(_tn).trs)), \
	     echo; echo 'FAIL: $(_tn)'; echo $(call _sp,FAIL: $(_tn)); \
	     echo; cat '$(_tn).log';)) \
	} > $(TEST_SUITE_LOG)
	$(eval c_res := $(if $(filter FAIL,$(_res)),$(c_red),$(c_green)))
	@echo $(c_res)$(_br); \
	 echo "Testsuite summary for $(name) $(version)"; \
	 echo $(_br)$(c_end); \
	 $(call _tests_summary,$(_res)) \
	 echo $(c_res)$(_br); \
	 echo "See $(TEST_SUITE_LOG)"; \
	 echo $(_br)$(c_end); \
	 $(if $(filter FAIL,$(_res)), \
	   $(if $(VERBOSE_TESTS),cat $(TEST_SUITE_LOG);) exit 1)

distcheck: $(distdir).tar.xz
	@$(TAR) xf $<
	@$(CHMOD) -R -w $(distdir)
	@$(MKDIR) _build _inst
	@echo && echo === configure === && echo
	@cd _build && MAKELEVEL= MAKEFLAGS= \
	  eval ../$(distdir)/configure $$DISTCHECK_CONFIGURE_FLAGS
	@$(foreach g,all check install uninstall distclean, \
	  echo && echo === make $g === && echo && \
	  $(MAKE) $(_npd) -C _build $g DESTDIR=../_inst &&) echo
	@$(FIND) _inst -type d -delete
	@$(RMDIR) _build || { $(FIND) _build; exit 1; }
	@$(CHMOD) -R +w $(distdir)
	@$(RM) -r $(distdir)

# --- sanitizers ---------------------------------------------------------------

_asan_flags = -fsanitize=address -fsanitize=pointer-compare,pointer-subtract
_lsan_flags = -fsanitize=leak
_tsan_flags = -fsanitize=thread
_ubsan_flags = -fsanitize=undefined -fno-sanitize-recover=all

define _san
  configs += $1
  $1-disabled = y
  ifneq ($1,lsan)
    cflags += $$(if $$(have_$1),$$(_$1_flags))
    cxxflags += $$(if $$(have_$1),$$(_$1_flags))
  endif
  ldflags += $$(if $$(have_$1),$(firstword $(_$1_flags)))
  have_san := $$(or $$(have_san),$$(have_$1))
  ld_preload_san += $$(if $$(have_$1),$(filter /%,\
    $(shell $(CC) -print-file-name=lib$1.$(_so)) \
    $(shell $(CC) -print-file-name=libclang_rt.$1_osx_dynamic.dylib)))
endef

$(foreach san,asan lsan tsan ubsan,$(eval $(call _san,$(san))))

# --- config -------------------------------------------------------------------

.PHONY: configure config config-deps config-env config.mk

_have_list = $(foreach c,$1,have_$c $(if $(have_$c),$(foreach v,$2,$v_$c)))

_config_env_vars = \
  CROSS CC CXX CPP AR PKG_CONFIG INSTALL MKDIR RMDIR NASM TAR RM FIND CHMOD \
  CFLAGS CXXFLAGS CPPFLAGS NASMFLAGS LDFLAGS VERBOSE \
  prefix exec_prefix bindir sbindir libdir libexecdir datarootdir \
  datadir includedir pkgconfigdir

_config_vars = $(_config_env_vars) \
  $(call _have_list,$(sort $(configs)),cppflags ldflags ldlibs) \
  $(call _have_list,$(_pkgs),version cppflags ldlibs prefix)

_alldirs = $(if $(filter-out ./,$(dir $1)),\
  $(call _alldirs,$(patsubst %/,%,$(dir $1))) $(dir $1))

_makefiles = $(addsuffix Makefile,$(sort $(foreach d,$(_dirs),$(call _alldirs,$d))))
_ld_path = $(subst $() ,:,$(addprefix $(CURDIR)/,$(sort $(dir $(_lib-targets)))))

configure: config config.mk $(_makefiles)
ifdef _oot
	@M="$(firstword $(MAKEFILE_LIST))"; \
	{ echo ".PHONY: all"; \
	  echo "MAKEFLAGS += -rR"; \
	  echo "all:;@\$$(MAKE) $(_npd) -f $$M \$$(MAKECMDGOALS)"; \
	  echo "\$$(filter-out all,\$$(MAKECMDGOALS)): all"; } > Makefile
endif
	@{ echo '#!/bin/sh'; \
	   echo export $(_DY)LD_LIBRARY_PATH=\"$(_ld_path):\$$$(_DY)LD_LIBRARY_PATH\"; \
	   echo 'exec "$$@"'; \
	} > run
	@$(CHMOD) +x run

$(_makefiles):
	@$(MKDIR) -p $(@D)
	@M="$(firstword $(MAKEFILE_LIST))"; \
	C="$(subst $() ,/,$(patsubst %,..,$(subst /, ,$(@D))))"; \
	{ echo ".PHONY: all"; \
	  echo "MAKEFLAGS += -rR"; \
	  echo "all:;@\$$(MAKE) $(_npd) -C $$C -f $$M \$$(MAKECMDGOALS) _sub=$(@D)/"; \
	  echo "\$$(filter-out all,\$$(MAKECMDGOALS)): all"; } > $@

config.mk:
	@{ $(foreach v,$(_config_vars),echo "$v = $($v)";) } > $@

config:
	@$(foreach c,$(sort $(configs) $(_pkgs)), \
	  echo " $(if $(have_$c),$(c_green)✓$(c_end), ) \
	  $c$(if $(version_$c), $(c_gray)$(version_$c) $(prefix_$c)$(c_end))";)

config-deps:
	@$(foreach t,$(sort $(_targets)), \
	  $(if $(strip $($t-deps) $($t-libs) $($t-opt-libs)),\
	  echo " $(if $(have_$(notdir $t)),$(c_green)✓$(c_end), ) $(notdir $t) \
	  ($(foreach c,$(sort $($t-deps) $($t-libs) $($t-opt-libs)),$(if \
	  $(have_$c),$(c_green),$(c_red))$c$(c_end)))";))

config-env:
	@$(foreach v,$(_config_env_vars),printf "%-15s: %s\n" $v '$($v)';)

[a-z] = a b c d e f g h i j k l m n o p q r s t u v w x y z
[A-Z] = A B C D E F G H I J K L M N O P Q R S T U V W X Y Z

_tr = $(strip $(if $1,$(subst $(firstword $1),$(firstword $2),\
  $(call _tr,$(wordlist 2,26,$1),$(wordlist 2,26,$2),$3)),$3))
_toupper = $(call _tr,$([a-z]),$([A-Z]),$1)
_toid = $(call _tr,- . /,_ _ _,$1)
_have = $(sort $(foreach c,$(configs) $(_pkgs),\
  $(if $(have_$c),$(call _toid,$(call _toupper,$c)))))

print-config.h = CONFIG   $@
cmd-config.h = for i in $(_have); do echo "\#define HAVE_$$i 1"; done > $@

_distcleanfiles = config.mk config.log $(if $(_oot),Makefile) $(_makefiles) run

# --- dependencies -------------------------------------------------------------

_CPPFLAGS += -MD -MP

-include $(_depfiles)

_lib_deps = $(foreach l,$($1-libs) $($1-opt-libs),$(filter $l %/$l,$(_lib-targets)))

$(_bin-targets): %: $$(call _tsubst,%,.o) | $$(call _lib_deps,%); $(call cmd,ld)
$(_lib-targets:=_pic.a): %_pic.a: $$(call _tsubst,%,-pic.o); $(call cmd,ar)
$(_lib-targets:=.a): %.a: $$(call _tsubst,%,.o); $(call cmd,ar)
$(_lib-targets:=.$(_so)): %.$(_so): $$(call _tsubst,%,-pic.o) | $$(call _lib_deps,%)
	$(call cmd,ld)

$(foreach d,$(_dirs),$(eval $(filter $d%,$(_out)): | $d))

# --- help ---------------------------------------------------------------------

.PHONY: help help-config help-pkgs help-config-list
help:
	@echo "Available targets"
	@echo "-----------------"
	@echo "all         : Build targets to be installed"
	@echo "all-tests   : Build all targets including tests"
	@echo "configure   : Save configuration to config.mk"
	@echo "config      : Show configured options"
	@echo "config-deps : Show targets dependencies"
	@echo "config-env  : Show configured environment variables"
	@echo "check       : Run the test suite"
	@echo "dist        : Create distribution archive"
	@echo "distcheck   : Check distribution archive"
	@echo "install     : Install targets to destination directories"
	@echo "uninstall   : Remove targets from destination directories"
	@echo "clean       : Delete all target files"
	@echo "distclean   : Delete configuration files, targets and directories"
	@echo "help        : Show this help message"
	@echo
	@$(MAKE) $(_npd) help-config
	@echo
	@$(MAKE) $(_npd) help-pkgs

help-config:
	@echo "Configuration options"
	@echo "---------------------"
	@for c in $(sort $(configs)); do echo $$c; done | column

help-pkgs:
	@echo "Build dependencies"
	@echo "------------------"
	@for p in $(sort $(_pkgs)); do echo $$p; done | column

help-config-list:
	@$(foreach c,$(sort $(configs)),echo $(if $($c-disabled),-,+)$c;)

# --- debug --------------------------------------------------------------------

.PHONY: $(filter debug.%,$(MAKECMDGOALS))
$(filter debug.%,$(MAKECMDGOALS)): debug.%:
	@:$(info $* $(if $(filter simple,$(flavor $*)),:)= $($*) from $(origin $*))
