configs += nasm
nasm-command = $(NASM)

configs += i686
i686-assert = __i686__

configs += x86_64
x86_64-assert = __x86_64__

#configs += x86asm
#x86asm-assert = __i686__ || __x86_64__
#x86asm-deps = nasm

have_x86asm = $(and $(have_nasm),$(or $(have_i686),$(have_x86_64)))

nasmflags = -f $(if $(have_apple),macho,elf)$(if $(have_x86_64),64,32) \
    $(if $(have_apple),-DPREFIX) $(if $(have_pic),-DPIC) \
    -Pconfig.asm -Ix86 -I$(top_srcdir)/x86

distfiles = config.asm.in x86inc.asm x86util.asm
genfiles = config.asm
#genfiles = $(if $(have_x86asm),config.asm)

ARCH_X86_64 = $(if $(have_x86_64),1,0)

$(builddir)/config.asm: $(srcdir)/config.asm.in
	$(call cmd,gen)$(SED) 's/@ARCH_X86_64@/$(ARCH_X86_64)/' < $< > $@
