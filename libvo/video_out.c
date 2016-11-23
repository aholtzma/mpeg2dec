/*
 * video_out.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "video_out.h"

//
// Externally visible list of all vo drivers
//

extern vo_functions_t video_out_x11;
extern vo_functions_t video_out_sdl;
extern vo_functions_t video_out_mga;
extern vo_functions_t video_out_3dfx;
extern vo_functions_t video_out_syncfb;
extern vo_functions_t video_out_null;
extern vo_functions_t video_out_pgm;
extern vo_functions_t video_out_md5;

vo_functions_t* video_out_drivers[] = 
{
#ifdef LIBVO_X11
	&video_out_x11,
#endif
#ifdef LIBVO_SDL
	&video_out_sdl,
#endif
#ifdef LIBVO_MGA
	&video_out_mga,
#endif
#ifdef LIBVO_3DFX
	&video_out_3dfx,
#endif
#ifdef LIBVO_SYNCFB
	&video_out_syncfb,
#endif
	&video_out_null,
	&video_out_pgm,
	&video_out_md5,
	NULL
};


//
// Here are the generic fallback routines that could
// potentially be used by more than one display driver
//


//FIXME this should allocate AGP memory via agpgart and then we
//can use AGP transfers to the framebuffer
vo_image_buffer_t* 
allocate_image_buffer_common (int width, int height, uint32_t format)
{
    vo_image_buffer_t *image;
    uint32_t image_size;

    //we only know how to do YV12 right now
    if (format != 0x32315659) return NULL;
	
    image = malloc(sizeof(vo_image_buffer_t));

    if(!image) return NULL;

    image->height = height;
    image->width = width;
    image->format = format;
	
    image_size = width * height * 3 / 2;
    image->base = malloc(image_size);

    if (!image->base) {
	free (image);
	return NULL;
    }

    return image;
}

void free_image_buffer_common(vo_image_buffer_t* image)
{
    free (image->base);
    free (image);
}
