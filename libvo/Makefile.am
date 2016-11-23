INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)

CFLAGS = @CFLAGS@ @LIBVO_CFLAGS@

noinst_LTLIBRARIES = libvo.la
libvo_la_SOURCES = video_out.c video_out_x11.c video_out_sdl.c \
		   video_out_mga.c video_out_3dfx.c video_out_syncfb.c \
		   video_out_null.c video_out_pgm.c video_out_md5.c \
		   yuv2rgb.c yuv2rgb_mlib.c yuv2rgb_mmx.c
libvo_la_LIBADD = @LIBVO_LIBS@

EXTRA_DIST = configure.incl video_out.h video_out_internal.h yuv2rgb.h
