/*
 * video_out_null.c
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

#include "config.h"
#include "video_out.h"
#include "video_out_internal.h"

LIBVO_EXTERN(null)


static vo_info_t vo_info = 
{
	"Null video output",
	"null",
	"Aaron Holtzman <aholtzma@ess.engr.uvic.ca>",
	""
};

static uint32_t image_width, image_height;

static uint32_t
draw_slice(uint8_t *src[], int slice_num)
{
	return 0;
}

static void
flip_page(void)
{
}

static uint32_t
draw_frame(uint8_t *src[])
{
	return 0;
}

static uint32_t
init(int width, int height, int fullscreen, char *title, uint32_t format)
{
	image_width = width;
	image_height = height;
	return 0;
}

static const vo_info_t*
get_info(void)
{
	return &vo_info;
}

static vo_image_buffer_t* 
allocate_image_buffer()
{
	//use the generic fallback
	return allocate_image_buffer_common(image_height,image_width,0x32315659);
}

static void	
free_image_buffer(vo_image_buffer_t* image)
{
	//use the generic fallback
	free_image_buffer_common(image);
}
