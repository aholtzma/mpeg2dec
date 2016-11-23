bin_PROGRAMS = mpeg2dec extract_mpeg2
mpeg2dec_SOURCES = mpeg2dec.c
mpeg2dec_LDADD = $(top_builddir)/libmpeg2/libmpeg2.la \
		 $(top_builddir)/libvo/libvo.la \
		 libmpeg2dec.la
extract_mpeg2_SOURCES = extract_mpeg2.c

lib_LTLIBRARIES = libmpeg2dec.la
libmpeg2dec_la_SOURCES = cpu_accel.c
libmpeg2dec_la_LDFLAGS = -no-undefined
