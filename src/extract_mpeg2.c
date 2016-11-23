/*
 * extract_mpeg2.c
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "mpeg2.h"
//#include "video_out.h"

#define BUFFER_SIZE 262144
static uint8_t buffer[BUFFER_SIZE];
static FILE *in_file;

static void print_usage (char * argv[])
{
    fprintf (stderr,"usage: %s file\n", argv[0]);

    exit (1);
}

static void handle_args (int argc, char * argv[])
{
    int c;

    if ((c = getopt (argc,argv,"")) != -1) {
	print_usage (argv);
    }

    if (optind < argc) {
	in_file = fopen (argv[optind], "r");
	if (!in_file) {
	    fprintf (stderr, "%s - couldnt open file %s\n", strerror (errno),
		     argv[optind]);
	    exit (1);
	}
    } else
	in_file = stdin;
}

static void ps_loop (void)
{
    static int mpeg1_skip_table[16] = {
	     1, 0xffff,      5,     10, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
    };

    uint8_t * buf;
    uint8_t * end;
    uint8_t * tmp1;
    uint8_t * tmp2;

    buf = buffer;

    do {
	end = buf + fread (buf, 1, buffer + BUFFER_SIZE - buf, in_file);
	buf = buffer;

	while (buf + 4 <= end) {
	    // check start code
	    if (buf[0] || buf[1] || (buf[2] != 0x01)) {
		fprintf (stderr, "missing start code\n");
		exit (1);
	    }

	    switch (buf[3]) {
	    case 0xb9:	/* program end code */
		return;
	    case 0xba:	/* pack header */
		/* skip */
		if ((buf[4] & 0xc0) == 0x40)	/* mpeg2 */
		    tmp1 = buf + 14 + (buf[13] & 7);
		else if ((buf[4] & 0xf0) == 0x20)	/* mpeg1 */
		    tmp1 = buf + 12;
		else if (buf + 5 > end)
		    goto copy;
		else {
		    fprintf (stderr, "weird pack header\n");
		    exit (1);
		}
		if (tmp1 > end)
		    goto copy;
		buf = tmp1;
		break;
	    case 0xe0:	/* video */
		tmp2 = buf + 6 + (buf[4] << 8) + buf[5];
		if (tmp2 > end)
		    goto copy;
		if ((buf[6] & 0xc0) == 0x80)	/* mpeg2 */
		    tmp1 = buf + 9 + buf[8];
		else {	/* mpeg1 */
		    for (tmp1 = buf + 6; *tmp1 == 0xff; tmp1++)
			if (tmp1 == buf + 6 + 16) {
			    fprintf (stderr, "too much stuffing\n");
			    buf = tmp2;
			    break;
			}
		    if ((*tmp1 & 0xc0) == 0x40)
			tmp1 += 2;
		    tmp1 += mpeg1_skip_table [*tmp1 >> 4];
		}
		if (tmp1 < tmp2)
		    fwrite (tmp1, tmp2-tmp1, 1, stdout);
		buf = tmp2;
		break;
	    default:
		if (buf[3] < 0xb9) {
		    fprintf (stderr,
			     "looks like a video stream, not system stream\n");
		    exit (1);
		}
		/* skip */
		tmp1 = buf + 6 + (buf[4] << 8) + buf[5];
		if (tmp1 > end)
		    goto copy;
		buf = tmp1;
		break;
	    }
	}

	if (buf < end) {
	copy:
	    /* we only pass here for mpeg1 ps streams */
	    memmove (buffer, buf, end - buf);
	}
	buf = buffer + (end - buf);

    } while (end == buffer + BUFFER_SIZE);
}

int main (int argc,char *argv[])
{
    handle_args (argc, argv);

    ps_loop ();

    return 0;
}
