/*
 * mpeg2.h
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

#ifdef __OMS__
#include <oms/plugin/output_video.h>
#ifndef vo_functions_t
#define vo_functions_t plugin_output_video_t
#endif
#else
//FIXME normally I wouldn't nest includes, but we'll leave this here until I get
//another chance to move things around
#include "video_out.h"
#endif

#include <inttypes.h>
#ifdef __OMS__
#include <oms/accel.h>
#else
#include "oms_accel.h"
#endif

//config flags
#define MPEG2_MLIB_ENABLE OMS_ACCEL_MLIB
#define MPEG2_MMX_ENABLE OMS_ACCEL_X86_MMX
#define MPEG2_3DNOW_ENABLE OMS_ACCEL_X86_3DNOW
#define MPEG2_SSE_ENABLE OMS_ACCEL_X86_MMXEXT

typedef struct mpeg2_config_s {
    //Bit flags that enable various things
    uint32_t flags;
} mpeg2_config_t;

void mpeg2_init (void);
int mpeg2_decode_data (vo_functions_t *, uint8_t * data_start, uint8_t * data_end);
void mpeg2_close (vo_functions_t *);
void mpeg2_drop (int flag);
void mpeg2_output_init (int flag);
