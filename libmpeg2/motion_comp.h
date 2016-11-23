/*
 * motion_comp.h
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

void motion_comp_init (void);

typedef struct mc_functions_s
{
    void (* put [8]) (uint8_t *dst, uint8_t *, int32_t, int32_t);
    void (* avg [8]) (uint8_t *dst, uint8_t *, int32_t, int32_t);
} mc_functions_t;

#define MOTION_COMP_EXTERN(x) mc_functions_t mc_functions_##x =\
{\
    {motion_comp_put_16x16_##x, motion_comp_put_x_16x16_##x,\
     motion_comp_put_y_16x16_##x, motion_comp_put_xy_16x16_##x,\
     motion_comp_put_8x8_##x, motion_comp_put_x_8x8_##x,\
     motion_comp_put_y_8x8_##x, motion_comp_put_xy_8x8_##x},\
    {motion_comp_avg_16x16_##x, motion_comp_avg_x_16x16_##x,\
     motion_comp_avg_y_16x16_##x, motion_comp_avg_xy_16x16_##x,\
     motion_comp_avg_8x8_##x, motion_comp_avg_x_8x8_##x,\
     motion_comp_avg_y_8x8_##x, motion_comp_avg_xy_8x8_##x}\
};

extern mc_functions_t mc_functions_c;
extern mc_functions_t mc_functions_mmx;
extern mc_functions_t mc_functions_mlib;
