/*
 * mpeg2dec.c
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
static uint32_t frame_counter = 0;

static struct timeval tv_beg, tv_end, tv_start;
static uint32_t elapsed;
static uint32_t total_elapsed;
static uint32_t last_count = 0;
static uint32_t demux_dvd = 0;
static vo_functions_t *video_out;

static void print_fps (int final) 
{
    int fps, tfps, frames;
	
    gettimeofday (&tv_end, NULL);

    if (frame_counter++ == 0) {
	tv_start = tv_beg = tv_end;
    }

    elapsed = (tv_end.tv_sec - tv_beg.tv_sec) * 100 +
	(tv_end.tv_usec - tv_beg.tv_usec) / 10000;
    total_elapsed = (tv_end.tv_sec - tv_start.tv_sec) * 100 +
	(tv_end.tv_usec - tv_start.tv_usec) / 10000;

    if (final) {
	if (total_elapsed) 
	    tfps = frame_counter * 10000 / total_elapsed;
	else
	    tfps = 0;

	fprintf (stderr,"\n%d frames decoded in %d.%02d "
		 "seconds (%d.%02d fps)\n", frame_counter,
		 total_elapsed / 100, total_elapsed % 100,
		 tfps / 100, tfps % 100);

	return;
    }

    if (elapsed < 50)	/* only display every 0.50 seconds */
	return;

    tv_beg = tv_end;
    frames = frame_counter - last_count;

    fps = frames * 10000 / elapsed;			/* 100x */
    tfps = frame_counter * 10000 / total_elapsed;	/* 100x */

    fprintf (stderr, "%d frames in %d.%02d sec (%d.%02d fps), "
	     "%d last %d.%02d sec (%d.%02d fps)\033[K\r", frame_counter,
	     total_elapsed / 100, total_elapsed % 100,
	     tfps / 100, tfps % 100, frames, elapsed / 100, elapsed % 100,
	     fps / 100, fps % 100);

    last_count = frame_counter;
}
 
static void signal_handler (int sig)
{
    print_fps (1);
    signal (sig, SIG_DFL);
    raise (sig);
}

static void print_usage (char * argv[])
{
    uint32_t i = 0;

    fprintf (stderr,"usage: %s [-o mode] [-s] file\n"
	     "\t-s\tsystem stream (.vob file)\n"
	     "\t-o\tvideo_output mode\n", argv[0]);

    while (video_out_drivers[i] != NULL) {
	const vo_info_t *info;
		
	info = video_out_drivers[i++]->get_info ();

	fprintf (stderr, "\t\t\t%s\t%s\n", info->short_name, info->name);
    }

    exit (1);
}

static void handle_args (int argc, char * argv[])
{
    int c;
    int i;

    while ((c = getopt (argc,argv,"so:")) != -1) {
	switch (c) {
	case 'o':
	    for (i=0; video_out_drivers[i] != NULL; i++) {
		const vo_info_t *info = video_out_drivers[i]->get_info ();

		if (strcmp (info->short_name,optarg) == 0)
		    video_out = video_out_drivers[i];
	    }
	    if (video_out == NULL)
		{
		    fprintf (stderr,"Invalid video driver: %s\n", optarg);
		    print_usage (argv);
		}
	    break;

	case 's':
	    demux_dvd = 1;
	    break;

	default:
	    print_usage (argv);
	}
    }

    // -o not specified, use a default driver 
    if (video_out == NULL)
	video_out = video_out_drivers[0];

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

    int num_frames;
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
		printf ("missing start code\n");
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
		    printf ("weird pack header\n");
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
			    printf ("too much stuffing\n");
			    buf = tmp2;
			    break;
			}
		    if ((*tmp1 & 0xc0) == 0x40)
			tmp1 += 2;
		    tmp1 += mpeg1_skip_table [*tmp1 >> 4];
		}
		if (tmp1 < tmp2) {
		    num_frames = mpeg2_decode_data (video_out, tmp1, tmp2);
		    while (num_frames--)
			print_fps (0);
		}
		buf = tmp2;
		break;
	    default:
		if (buf[3] < 0xb9) {
		    printf ("looks like a video stream, not system stream\n");
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

static void es_loop (void)
{
    uint8_t * end;
    int num_frames;
		
    do {
	end = buffer + fread (buffer, 1, BUFFER_SIZE, in_file);

	num_frames =
	    mpeg2_decode_data (video_out, buffer, end);

	while (num_frames--)
	    print_fps (0);

    } while (end == buffer + BUFFER_SIZE);
}

int main (int argc,char *argv[])
{
    printf (PACKAGE"-"VERSION" (C) 2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>\n");

    handle_args (argc,argv);

    signal (SIGINT, signal_handler);

    mpeg2_init ();

    gettimeofday (&tv_beg, NULL);

    if (demux_dvd)
	ps_loop ();
    else
	es_loop ();

    mpeg2_close (video_out);
    print_fps (1);
    return 0;
}
