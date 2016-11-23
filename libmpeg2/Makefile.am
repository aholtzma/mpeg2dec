INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/libvo

CFLAGS = @CFLAGS@ @LIBMPEG2_CFLAGS@

noinst_LTLIBRARIES = libmpeg2.la
libmpeg2_la_SOURCES = decode.c header.c stats.c debug.c slice.c \
		      motion_comp.c motion_comp_mlib.c motion_comp_mmx.c \
		      idct.c idct_mlib.c idct_mmx.c
# the source ordering seems to impact the speed.
# On my setup the following ordering is about 2% faster.
# I'll have to investigate...
# libmpeg2_la_SOURCES = slice.c header.c stats.c idct.c motion_comp.c \
# decode.c debug.c idct_mmx.c motion_comp_mmx.c
libmpeg2_la_LIBADD = @LIBMPEG2_LIBS@

EXTRA_DIST = configure.incl \
	     header.h stats.h debug.h slice.h vlc.h motion_comp.h \
	     idct.h idct_mlib.h idct_mmx.h mpeg2.h mpeg2_internal.h
