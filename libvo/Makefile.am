CFLAGS = @CFLAGS@ @LIBVO_CFLAGS@

lib_LTLIBRARIES = libvo.la
libvo_la_SOURCES = video_out.c video_out_x11.c video_out_sdl.c \
		   video_out_null.c video_out_pgm.c video_out_mga.c \
		   yuv2rgb.c yuv2rgb_mlib.c yuv2rgb_mmx.c
libvo_la_LIBADD = @LIBVO_LIBS@
libvo_la_LDFLAGS = -no-undefined

EXTRA_DIST = configure.incl video_out_internal.h yuv2rgb.h hw_bes.h
