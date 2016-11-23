INCLUDES = -I$(top_srcdir)/libmpeg2 -I$(top_srcdir)/libvo -I$(top_srcdir)/include

bin_PROGRAMS = mpeg2dec extract_mpeg2
mpeg2dec_SOURCES = mpeg2dec.c cpu_accel.c
mpeg2dec_LDADD = $(top_builddir)/libmpeg2/libmpeg2.la \
		 $(top_builddir)/libvo/libvo.la
extract_mpeg2_SOURCES = extract_mpeg2.c
