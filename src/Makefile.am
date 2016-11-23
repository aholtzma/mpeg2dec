CFLAGS = @CFLAGS@ @MPEG2DEC_CFLAGS@

bin_PROGRAMS = mpeg2dec extract_mpeg2
mpeg2dec_SOURCES = mpeg2dec.c getopt.c
mpeg2dec_LDADD = $(top_builddir)/libmpeg2/libmpeg2.la \
		 $(top_builddir)/libvo/libvo.a @LIBVO_LIBS@ \
		 libcpuaccel.la
extract_mpeg2_SOURCES = extract_mpeg2.c getopt.c

lib_LTLIBRARIES = libcpuaccel.la
libcpuaccel_la_SOURCES = cpu_accel.c
libcpuaccel_la_LDFLAGS = -no-undefined

EXTRA_DIST = configure.incl getopt.h
