#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AC_INIT([upipe],[0.5],[cmassiot@upipe.org])
AC_CONFIG_SRCDIR([lib/upipe/uprobe_stdio.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([autotools])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([foreign nostdinc parallel-tests color-tests subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
PKG_PROG_PKG_CONFIG

# Checks for programs.
AC_CHECK_PROGS(PERL, perl)
AC_PATH_PROG(MOC, [moc-qt4 moc], moc)
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O
AM_PROG_AS
LT_INIT([win32-dll])

AC_ARG_ENABLE([luajit],
              AS_HELP_STRING([--enable-luajit], [Enable build of luajit bindings]))
AM_CONDITIONAL(BUILD_LUAJIT,
               test "$enable_luajit" = yes -a "$OBJDUMP" != false -a "$enable_shared" != no)
AM_COND_IF(BUILD_LUAJIT,[
           AC_CHECK_PROGS(LUAJIT, luajit)
           AC_CHECK_PROGS(EU_READELF, eu-readelf)
           AC_CHECK_PROGS(LLVM_DWARFDUMP, [llvm-dwarfdump "xcrun llvm-dwarfdump"])])

AC_PATH_PROGS(NASM, [nasm yasm])

NASMFLAGS=""
ARCH_X86_64=0
case "${host_os}" in
    darwin*)
    case "${host}" in
        i?86*)
        NASMFLAGS="-f macho32 -DPREFIX"
        ;;
        x86_64*)
        NASMFLAGS="-f macho64 -DPREFIX"
        ARCH_X86_64=1
        ;;
    esac
    AC_DEFINE(EXTERN_ASM, _, Mangling prefix)
    ;;
    *mingw32*)
    case "${host}" in
        i?86*)
        NASMFLAGS="-f win32 -DPREFIX"
        ;;
        amd64*|x86_64*)
        NASMFLAGS="-f win64 -DPREFIX"
        ARCH_X86_64=1
        ;;
    esac
    AC_DEFINE(EXTERN_ASM, , Mangling prefix)
    ;;
    *)
    case "${host}" in
        i?86*)
        NASMFLAGS="-f elf32"
        ;;
        x86_64*)
        NASMFLAGS="-f elf64"
        ARCH_X86_64=1
        ;;
    esac
    AC_DEFINE(HAVE_SECTION_DATA_REL_RO, 1, Define to 1 if section data.rel.ro is supported)
    AC_DEFINE(EXTERN_ASM, , Mangling prefix)
    ;;
esac

AM_CONDITIONAL(HAVE_X86ASM, test -n "${NASM}" -a -n "${NASMFLAGS}")
AM_COND_IF(HAVE_X86ASM, AC_DEFINE(HAVE_X86ASM, 1, Define to 1 if an x86 assembler is available))

# add -prefer-non-pic so libtool doesn't add -fPIC, which nasm doesn't understand
NASMFLAGS="${NASMFLAGS} -DPIC -prefer-non-pic -Pconfig.asm -I\$(top_builddir)/x86/ -I\$(top_srcdir)/x86/"

WARN_FLAGS="-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wmissing-declarations -Wwrite-strings -Wredundant-decls -Wundef -Wmissing-include-dirs"
WARN_FLAGS_C="-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition"
WARN_FLAGS_OPT="-Wduplicated-cond -Wduplicated-branches -Wrestrict -Wformat=2 -Wno-vla-larger-than"
# TODO: add -Wnull-dereference and -Wlogical-op once all warnings are fixed
for flag in $WARN_FLAGS_OPT; do
    AX_CHECK_COMPILE_FLAG([$flag],[WARN_FLAGS="$WARN_FLAGS $flag"],,-Werror)
done

AC_SUBST([AM_CFLAGS], ["$WARN_FLAGS $WARN_FLAGS_C"])
AC_SUBST([AM_CXXFLAGS], ["$WARN_FLAGS"])
AC_SUBST(NASMFLAGS)
AC_SUBST(ARCH_X86_64)
AM_CONDITIONAL(ARCH_AARCH64, [test ${host_cpu} = aarch64])

# Checks for libraries.
AC_CHECK_LIB(rt, clock_gettime, libadd_rt_lib="-lrt", libadd_rt_lib="")
AC_SUBST(libadd_rt_lib)
AX_PTHREAD(AM_CONDITIONAL(HAVE_PTHREAD, true),
           AM_CONDITIONAL(HAVE_PTHREAD, false))
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_LIBS)
AM_ICONV

# Check for header, then libev3 (ev_loop), then libev4 (ev_run)
AC_CHECK_HEADERS([ev.h], [
        AC_CHECK_LIB(ev, ev_loop,
                AM_CONDITIONAL(HAVE_EV, true),[
                AC_CHECK_LIB(ev, ev_run,
                        AM_CONDITIONAL(HAVE_EV, true),
                        AM_CONDITIONAL(HAVE_EV, false))])],
        AM_CONDITIONAL(HAVE_EV, false))
AC_CHECK_HEADERS([dvbcsa/dvbcsa.h],
                 AM_CONDITIONAL(HAVE_DVBCSA, true),
                 AM_CONDITIONAL(HAVE_DVBCSA, false))
AC_CHECK_HEADERS([bearssl.h],
                 AM_CONDITIONAL(HAVE_BEARSSL, true),
                 AM_CONDITIONAL(HAVE_BEARSSL, false))

PKG_CHECK_UPIPE(GCRYPT, libgcrypt, [gcrypt.h])
PKG_CHECK_EXISTS([libgcrypt], [LIBUPIPE_TS_PKGCONFIG_REQUIRES="$LIBUPIPE_TS_PKGCONFIG_REQUIRES libgcrypt"])
PKG_CHECK_UPIPE(TASN1, libtasn1, [libtasn1.h])
PKG_CHECK_EXISTS([libtasn1], [LIBUPIPE_TS_PKGCONFIG_REQUIRES="$LIBUPIPE_TS_PKGCONFIG_REQUIRES libtasn1"])
PKG_CHECK_UPIPE(BITSTREAM, bitstream >= 1.4, [bitstream/common.h])
PKG_CHECK_UPIPE(AVUTIL, libavutil, [libavutil/avutil.h])
PKG_CHECK_UPIPE(AVFORMAT, [libavformat >= 53.32.0 libavcodec libavutil], [libavformat/avformat.h libavformat/avio.h libavutil/avutil.h])
PKG_CHECK_UPIPE(AVFILTER, libavfilter >= 6.82.100 libavutil, [libavfilter/avfilter.h libavutil/avutil.h])
PKG_CHECK_UPIPE(SWSCALE, libswscale >= 2.1.0 libavutil, [libswscale/swscale.h libavutil/avutil.h])
PKG_CHECK_UPIPE(SWRESAMPLE, libswresample libavutil, [libswresample/swresample.h libavutil/avutil.h])
PKG_CHECK_UPIPE(SPEEXDSP, speexdsp, [speex/speex_resampler.h])
PKG_CHECK_UPIPE(GLX, gl glu x11, [GL/glx.h GL/glu.h GL/gl.h])
PKG_CHECK_UPIPE(X264, x264, [x264.h])
PKG_CHECK_UPIPE(X265, x265, [x265.h])
PKG_CHECK_UPIPE(ECORE, ecore, [Ecore.h])
PKG_CHECK_UPIPE(ZVBI, zvbi-0.2, [libzvbi.h])
PKG_CHECK_UPIPE(FREETYPE, freetype2, [ft2build.h])
PKG_CHECK_UPIPE(EBUR128, libebur128, [ebur128.h])
PKG_CHECK_UPIPE(DVBV5, libdvbv5, [libdvbv5/dvb-dev.h])
PKG_CHECK_UPIPE(SRT, srt, [srt/srt.h])
AC_LANG_PUSH([C++])
PKG_CHECK_UPIPE(QTWEBKIT, QtWebKit, [QtWebKit])
AC_LANG_POP([C++])
PKG_CHECK_UPIPE(OPENSSL, openssl, [openssl/ssl.h])

# if have gcrypt and tasn then
AC_SUBST(LIBUPIPE_TS_PKGCONFIG_REQUIRES)

CFLAGS="${CFLAGS} -D_REENTRANT"
LIBS="${LIBS} -lpthread"

# x264-obe (with speedcontrol)
CFLAGS_save="$CFLAGS" LIBS_SAVE="$LIBS"
CFLAGS="$CFLAGS $X264_CFLAGS" LIBS="$LIBS $X264_LIBS"
AC_CHECK_FUNC(x264_speedcontrol_sync,
                AM_CONDITIONAL(HAVE_X264_OBE, true),
                AM_CONDITIONAL(HAVE_X264_OBE, false)
)
CFLAGS="$CFLAGS_save" LIBS="$LIBS_save"

# x262 (x264 mpeg2video encoding)
CFLAGS_save="$CFLAGS" LIBS_save="$LIBS"
CFLAGS="$CFLAGS $X264_CFLAGS" LIBS="$LIBS $X264_LIBS"
AC_CHECK_FUNC(x264_param_default_mpeg2,
                AM_CONDITIONAL(HAVE_X264_MPEG2, true),
                AM_CONDITIONAL(HAVE_X264_MPEG2, false)
)
CFLAGS="$CFLAGS_save" LIBS="$LIBS_save"


AC_CHECK_FUNC(writev,
                AM_CONDITIONAL(HAVE_WRITEV, true),
                AM_CONDITIONAL(HAVE_WRITEV, false)
)
AC_CHECK_HEADERS([ppapi/c/ppb.h], AM_CONDITIONAL(HAVE_NACL, true), AM_CONDITIONAL(HAVE_NACL, false))
AM_CONDITIONAL(HAVE_OSX_DARWIN, false)
AC_CHECK_HEADERS([AudioToolbox/AudioToolbox.h], [AM_CONDITIONAL(HAVE_AUDIOTOOLBOX, true) AM_CONDITIONAL(HAVE_OSX_DARWIN, true)], AM_CONDITIONAL(HAVE_AUDIOTOOLBOX, false))
AC_CHECK_HEADERS([alsa/asoundlib.h], AM_CONDITIONAL(HAVE_ALSA, true), AM_CONDITIONAL(HAVE_ALSA, false))
AC_CHECK_HEADERS([dlfcn.h], AM_CONDITIONAL(HAVE_DLFCN_H, true), AM_CONDITIONAL(HAVE_DLFCN_H, false))
AC_CHECK_HEADERS([amt.h], AM_CONDITIONAL(HAVE_AMT, true), AM_CONDITIONAL(HAVE_AMT, false))
AC_CHECK_HEADERS([net/netmap.h], AM_CONDITIONAL(HAVE_NETMAP, true), AM_CONDITIONAL(HAVE_NETMAP, false),[#include <stdint.h>
#include <net/if.h>])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h unistd.h sys/ioctl.h semaphore.h features.h net/if.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_CHECK_TYPES([ptrdiff_t])
AC_C_BIGENDIAN

# Checks for library functions.
AC_CHECK_FUNCS([memmove memset malloc realloc strdup pipe])

# Custom checks
AC_MSG_CHECKING([for C compiler atomic builtins])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[
                int i, x = 0;
                i = __atomic_fetch_add(&x, 1, __ATOMIC_SEQ_CST);
        ]])
],[
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_ATOMIC_OPS, 1, Define if compiler supports atomic operations.)
],[
        AC_MSG_RESULT([no])
])

AC_MSG_CHECKING(for timespec in sys/time.h)
AC_EGREP_HEADER(timespec,sys/time.h,[
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_STRUCT_TIMESPEC, 1,
                  Define if <sys/time.h> defines struct timespec.)
],[
        AC_MSG_RESULT(no)
])

AC_MSG_CHECKING([for eventfd])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
        [[#include <sys/eventfd.h>]],
        [[eventfd (0, 0);]])
],[
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_EVENTFD, 1, Define if the OS supports eventfd(2).)
],[
        AC_MSG_RESULT([no])
])

AC_MSG_CHECKING([for as func])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
        [[]],
        [[ asm(".func test\n\t.endfunc"); ]])
],[
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_AS_FUNC, 1, Define if the assembly supports .func)
],[
        AC_MSG_RESULT([no])
])

AC_SUBST(SHREXT, "$shrext")

AC_CONFIG_FILES([Makefile
                 include/Makefile
                 include/upipe/Makefile
                 include/upump-ev/Makefile
                 include/upump-ecore/Makefile
                 include/upump-srt/Makefile
                 include/upipe-modules/Makefile
                 include/upipe-freetype/Makefile
                 include/upipe-pthread/Makefile
                 include/upipe-framers/Makefile
                 include/upipe-av/Makefile
                 include/upipe-swscale/Makefile
                 include/upipe-ts/Makefile
                 include/upipe-dvb/Makefile
                 include/upipe-gl/Makefile
                 include/upipe-filters/Makefile
                 include/upipe-x264/Makefile
                 include/upipe-x265/Makefile
                 include/upipe-osx/Makefile
                 include/upipe-alsa/Makefile
                 include/upipe-swresample/Makefile
                 include/upipe-speexdsp/Makefile
                 include/upipe-blackmagic/Makefile
                 include/upipe-qt/Makefile
                 include/upipe-nacl/Makefile
                 include/upipe-amt/Makefile
                 include/upipe-v210/Makefile
                 include/upipe-hbrmt/Makefile
                 include/upipe-hls/Makefile
                 include/upipe-zvbi/Makefile
                 include/upipe-dveo/Makefile
                 include/upipe-netmap/Makefile
                 include/upipe-dvbcsa/Makefile
                 include/upipe-ebur128/Makefile
                 include/upipe-bearssl/Makefile
                 include/upipe-openssl/Makefile
                 lib/Makefile
                 lib/upipe/Makefile
                 lib/upipe/libupipe.pc
                 lib/upump-ev/Makefile
                 lib/upump-ev/libupump_ev.pc
                 lib/upump-ecore/Makefile
                 lib/upump-ecore/libupump_ecore.pc
                 lib/upump-srt/Makefile
                 lib/upump-srt/libupump_srt.pc
                 lib/upipe-freetype/Makefile
                 lib/upipe-freetype/libupipe_freetype.pc
                 lib/upipe-modules/Makefile
                 lib/upipe-modules/libupipe_modules.pc
                 lib/upipe-pthread/Makefile
                 lib/upipe-pthread/libupipe_pthread.pc
                 lib/upipe-framers/Makefile
                 lib/upipe-framers/libupipe_framers.pc
                 lib/upipe-av/Makefile
                 lib/upipe-av/libupipe_av.pc
                 lib/upipe-swscale/Makefile
                 lib/upipe-swscale/libupipe_swscale.pc
                 lib/upipe-ts/Makefile
                 lib/upipe-ts/libupipe_ts.pc
                 lib/upipe-dvb/Makefile
                 lib/upipe-dvb/libupipe_dvb.pc
                 lib/upipe-gl/Makefile
                 lib/upipe-gl/libupipe_gl.pc
                 lib/upipe-filters/Makefile
                 lib/upipe-filters/libupipe_filters.pc
                 lib/upipe-x264/Makefile
                 lib/upipe-x264/libupipe_x264.pc
                 lib/upipe-x265/Makefile
                 lib/upipe-x265/libupipe_x265.pc
                 lib/upipe-osx/Makefile
                 lib/upipe-osx/libupipe_osx.pc
                 lib/upipe-alsa/Makefile
                 lib/upipe-alsa/libupipe_alsa.pc
                 lib/upipe-swresample/Makefile
                 lib/upipe-swresample/libupipe_swresample.pc
                 lib/upipe-speexdsp/Makefile
                 lib/upipe-speexdsp/libupipe_speexdsp.pc
                 lib/upipe-blackmagic/Makefile
                 lib/upipe-blackmagic/libupipe_blackmagic.pc
                 lib/upipe-qt/Makefile
                 lib/upipe-qt/libupipe_qt.pc
                 lib/upipe-nacl/Makefile
                 lib/upipe-nacl/libupipe_nacl.pc
                 lib/upipe-amt/Makefile
                 lib/upipe-amt/libupipe_amt.pc
                 lib/upipe-v210/Makefile
                 lib/upipe-v210/libupipe_v210.pc
                 lib/upipe-hbrmt/Makefile
                 lib/upipe-hbrmt/libupipe_hbrmt.pc
                 lib/upipe-hls/Makefile
                 lib/upipe-hls/libupipe_hls.pc
                 lib/upipe-zvbi/Makefile
                 lib/upipe-zvbi/libupipe_zvbi.pc
                 lib/upipe-dveo/Makefile
                 lib/upipe-dveo/libupipe_dveo.pc
                 lib/upipe-netmap/Makefile
                 lib/upipe-netmap/libupipe_netmap.pc
                 lib/upipe-dvbcsa/Makefile
                 lib/upipe-dvbcsa/libupipe_dvbcsa.pc
                 lib/upipe-ebur128/Makefile
                 lib/upipe-ebur128/libupipe_ebur128.pc
                 lib/upipe-bearssl/Makefile
                 lib/upipe-bearssl/libupipe_bearssl.pc
                 lib/upipe-openssl/Makefile
                 lib/upipe-openssl/libupipe_openssl.pc
                 x86/Makefile
                 x86/config.asm
                 tests/Makefile
                 tests/checkasm/Makefile
                 examples/Makefile
                 luajit/Makefile])
AC_OUTPUT

grep "^#define HAVE_" config.h | sed -e 's/HAVE_/UPIPE_HAVE_/' > include/upipe/config.h
grep "^#define WORDS_BIGENDIAN" config.h | sed -e 's/WORDS_BIGENDIAN/UPIPE_WORDS_BIGENDIAN/' >> include/upipe/config.h
