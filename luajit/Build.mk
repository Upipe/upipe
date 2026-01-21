# --- external tools -----------------------------------------------------------

DSYMUTIL        ?= dsymutil
EU_READELF      ?= eu-readelf
LLVM_DWARFDUMP  ?= llvm-dwarfdump
LUAJIT          ?= luajit
OBJDUMP         ?= objdump
PERL            ?= perl

# --- config checks ------------------------------------------------------------

configs += eu-readelf
eu-readelf-command = $(EU_READELF)

configs += llvm-dwarfdump
llvm-dwarfdump-command = $(LLVM_DWARFDUMP)

configs += luajit
luajit-command = $(LUAJIT)
luajit-disabled = y

configs += objdump
objdump-command = $(OBJDUMP)

# --- targets ------------------------------------------------------------------

vpath %.lua $(top_srcdir)

LUAJIT_INSTALL_LMOD = $(datarootdir)/lua/5.1

so = $(_so)
dsym = $(if $(filter dylib,$(so)),.dSYM)

modules := $(subst _,-,$(patsubst lib%,%,$(notdir $(_lib-targets))))
includes = $(sort $(lib/$1/lib$(subst -,_,$1)-includes$(includes-transform)))
enabled-modules = $(foreach m,libupipe $(modules),$(if $(have_$m.lua),$m))
enabled-cdef-lua = $(foreach m,$(enabled-modules) upipe-helper,$m.lua)
enabled-includes = $(foreach m,upipe $(enabled-modules),$(call includes,$m))

dist-targets = upipe.lua ffi-stdarg.lua

data-targets = \
    $(patsubst upipe.lua,libupipe.lua,$(modules:=.lua)) \
    upipe-helper.lua $(modules:=-sigs.lua) \
    upipe-getters.lua uref-getters.lua \
    uprobe-event-args.lua upipe-command-args.lua

distfiles = gen-ffi-cdef.pl gen-args.pl libc.defs
cleanfiles = upipe.defs

module_ldflags = $(if $(have_apple),\
                 $(call -Wl,-undefined,dynamic_lookup),\
                 $(call -Wl,-z,undefs))
module_cflags = -Wno-missing-declarations \
                -Wno-missing-prototypes \
                -Wno-redundant-decls

lib-targets += libupipe-helper
libupipe-helper-src     = upipe-helper.c
libupipe-helper-cflags  = $(module_cflags)
libupipe-helper-opt     = dynamic
libupipe-helper-deps    = shared luajit

lib-targets += libffi-stdarg
libffi-stdarg-src     = ffi-stdarg.c
libffi-stdarg-cflags  = $(module_cflags)
libffi-stdarg-opt     = dynamic
libffi-stdarg-deps    = shared luajit

print-debug-info  = DWARF    $@
print-cdef        = CDEF     $@

cmd-debug-info = \
    $(if $(dsym),$(DSYMUTIL) $<;) \
    $(if $(have_eu-readelf),$(EU_READELF) -winfo -N, \
    $(if $(have_llvm-dwarfdump),$(LLVM_DWARFDUMP) -debug-info --show-form, \
    $(if $(have_objdump),$(OBJDUMP) --dwarf=info))) $<$(dsym) > $@ \
    $(if $(dsym), && $(RM) -r "$<$(dsym)")

fmt-debug-info = \
    $(if $(have_eu-readelf),eu-readelf, \
    $(if $(have_llvm-dwarfdump),llvm-dwarfdump, \
    $(if $(have_objdump),objdump)))

%.debug_info: lib%.$$(so); $(call cmd,debug-info)

so_ver = $(if $(or $(have_apple),$(filter dynamic,$($1-opt))),,\
         $(or $($1-so-version),0.0.0))
so_maj = $(if $(so_ver),.$(word 1,$(subst ., ,$(so_ver))))

define def-module
  lib-targets += lib$1.static
  lib$1.static-src     = $1.static.c
  lib$1.static-gen     = $1.static.c
  lib$1.static-cflags  = $$(module_cflags)
  lib$1.static-ldflags = $$(module_ldflags)
  lib$1.static-opt     = dynamic
  lib$1.static-deps    = shared luajit lib$(subst -,_,$1)

  $(patsubst upipe,libupipe,$1).lua-gen = $1.debug_info $1.static.debug_info
  $(patsubst upipe,libupipe,$1).lua-deps = lib$(subst -,_,$1)
  $(patsubst upipe,libupipe,$1)-sigs.lua-deps = lib$(subst -,_,$1)

  $1_soname = lib$(subst -,_,$1).$$(so)$(call so_maj,lib$(subst -,_,$1))

$(builddir)/$1.debug_info: lib/$1/lib$(subst -,_,$1).$$$$(so)
	$$(call cmd,debug-info)
endef

$(foreach m,$(modules),$(eval $(call def-module,$m)))
$(foreach d,$(data-targets) $(dist-targets), \
    $(eval $d-deps += shared luajit) \
    $(eval $d-dest = $(LUAJIT_INSTALL_LMOD)))

libupipe-av.static-libs += libavutil
libupipe-av.static-cppflags += -include libavutil/avutil.h
upipe-helper.lua-gen = upipe-helper.debug_info

# --- recipes ------------------------------------------------------------------

prefix_base    = upipe_ uprobe_ ubuf_ uref_ uchain_ upump_ ustring_ udict_ \
                 uclock_ umem_ uuri_ uqueue_ urequest_ ufifo_ ucookie_ \
                 urefcount_ ueventfd_ uring_ ubase_ urational_ uatomic_ \
                 ulifo_ upool_ ulist_ udeal_ ulog_

prefix_modules = upipe_ uprobe_ uref_ upump_

# override enum prefix
enum_upipe-ts = uprobe_ts_
enum_upipe-hls = uprobe_hls_
enum_upipe-filters = uprobe_filters_
enum_upipe-bearssl = uprobe_https_bearssl_
enum_upipe-openssl = uprobe_https_openssl_
enum_upipe-framers = uref_h26x_ uref_mpga_

$(builddir)/libupipe.lua: $(srcdir)/gen-ffi-cdef.pl $(srcdir)/libc.defs \
    $(builddir)/upipe.debug_info $(builddir)/upipe.static.debug_info
	$(call cmd,cdef)$(PERL) $< \
	  --format $(fmt-debug-info) \
	  --output $@ \
	  --read-defs $(srcdir)/libc.defs \
	  --write-defs $(builddir)/upipe.defs \
	  --enum ubase_err \
	  --enum upipe_command \
	  --enum upipe_mgr_command \
	  --enum uref_date_type \
	  --enum uprobe_event \
	  --enum 'urequest_*' \
	  --enum 'upump_*' \
	  $(addprefix --prefix ,$(prefix_base)) \
	  --load $(upipe_soname) \
	  --load libupipe.static.$(so) \
	  $(filter %.debug_info,$^)

$(builddir)/upipe.defs: $(builddir)/libupipe.lua

$(builddir)/upipe-helper.lua: $(srcdir)/gen-ffi-cdef.pl $(srcdir)/libc.defs \
    $(builddir)/upipe.defs $(builddir)/upipe-helper.debug_info
	$(call cmd,cdef)$(PERL) $< \
	  --format $(fmt-debug-info) \
	  --output $@ \
	  $(addprefix --read-defs ,$(filter %.defs,$^)) \
	  --prefix upipe_helper_ \
	  --struct upipe_helper_mgr \
	  --load libupipe-helper.$(so) \
	  $(filter %.debug_info,$^)

modules-cdef = $(patsubst %,$(builddir)/%.lua,$(filter-out upipe,$(modules)))

$(modules-cdef): %.lua: $(srcdir)/gen-ffi-cdef.pl $(srcdir)/libc.defs \
    $(builddir)/upipe.defs %.debug_info %.static.debug_info
	$(call cmd,cdef)$(PERL) $< \
	  --format $(fmt-debug-info) \
	  --output $@ \
	  $(addprefix --read-defs ,$(filter %.defs,$^)) \
	  --enum '$(or $(enum_$(*F)),uprobe_)*' \
	  $(addprefix --prefix ,$(prefix_modules)) \
	  --load $($(*F)_soname) \
	  --load lib$(*F).static.$(so) \
	  --require $(*F)-sigs \
	  $(filter %.debug_info,$^)

%-getters.lua: $$(addprefix $(builddir)/,$$(enabled-cdef-lua))
	$(call cmd,gen){ \
	  echo "return {"; \
	  $(SED) -n 's/^int $(*F)_\(\(.*_\)\{0,1\}get_.*\)(struct $(*F) \*, \([^,]*[^ ]\) *\*);$$/    \1 = "\3",/p' $^; \
	  echo "}"; \
	} > $@

%-args.lua: $(srcdir)/gen-args.pl $$(enabled-includes)
	$(call cmd,gen)$(PERL) $< $(*F) $(filter-out $<,$^) > $@

%.static.c: $$(call includes,$$(*F))
	$(call cmd,gen){ \
	  echo "#define static"; \
	  echo "#define inline"; \
	  $(foreach i,$^,echo '#include "$(patsubst $(abspath $(top_srcdir)/include)/%,%,$(abspath $i))"';) \
	  echo "void _("; \
	  $(SED) -n 's/^enum  *\([^ ]*\) .*/enum \1 _\1,/p' $^; \
	  echo "int _){}"; \
	} > $@

%-sigs.lua: $$(call includes,$$(*F))
	$(call cmd,gen)$(PERL) -ne \
	  'print if s/^#define UPIPE_(.*)_SIGNATURE UBASE_FOURCC\((.*)\)/upipe_sig("\L\1\E", \2)/' \
	  $^ > $@

# --- test suite ---------------------------------------------------------------

lua_path := $(abspath $(builddir)/?.lua);$(abspath $(srcdir)/?.lua)
test.ts := $(abspath $(top_srcdir)/tests/upipe_ts_test.ts)

_LD_PRELOAD = $(if $(have_apple),DYLD_INSERT_LIBRARIES,LD_PRELOAD)

log-compiler-lua = $(LUAJIT)
log-env-lua = $(log-env) LUA_PATH="$(lua_path)" \
  $(if $(ld_preload_san),\
    $(_LD_PRELOAD)="$(subst $() ,:,$(sort $(ld_preload_san)))") \
  $(if $(have_asan),ASAN_OPTIONS="detect_odr_violation=0") \
  $(if $(and $(have_apple),$(have_asan)),MallocNanoZone=0)

tests += check.lua
check.lua-args = $(enabled-modules)
check.lua-deps = luajit shared

tests += examples/upipe_duration.lua
examples/upipe_duration.lua-args = $(test.ts)
examples/upipe_duration.lua-deps = luajit shared

tests += examples/extract_pic.lua
examples/extract_pic.lua-args = $(test.ts) /dev/null
examples/extract_pic.lua-deps = luajit shared

tests += examples/upipe_xor.lua
examples/upipe_xor.lua-args = $(test.ts) /dev/null
examples/upipe_xor.lua-deps = luajit shared
