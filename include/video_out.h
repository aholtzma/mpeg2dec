/*
 * video_out.h
 * Copyright (C) 1999-2001 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
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

typedef struct vo_frame_s vo_frame_t;
typedef struct vo_instance_s vo_instance_t;

struct vo_frame_s {
    uint8_t * base[3];	/* pointer to 3 planes */
    void (* copy) (vo_frame_t * frame, uint8_t ** src);
    void (* field) (vo_frame_t * frame, int flags);
    void (* draw) (vo_frame_t * frame);
    vo_instance_t * instance;
};

typedef vo_instance_t * vo_open_t (void);

struct vo_instance_s {
    int (* setup) (vo_instance_t * this, int width, int height);
    void (* close) (vo_instance_t * this);
    vo_frame_t * (* get_frame) (vo_instance_t * this, int flags);
};

typedef struct vo_driver_s {
    char * name;
    vo_open_t * open;
} vo_driver_t;

void vo_accel (uint32_t accel);

/* return NULL terminated array of all drivers */
vo_driver_t * vo_drivers (void);

static vo_instance_t * vo_open (vo_open_t * open)
{
    return open ();
}

static int vo_setup (vo_instance_t * this, int width, int height)
{
    return this->setup (this, width, height);
}

static inline void vo_close (vo_instance_t * this)
{
    if (this->close)
	this->close (this);
}

#define VO_TOP_FIELD 1
#define VO_BOTTOM_FIELD 2
#define VO_BOTH_FIELDS (VO_TOP_FIELD | VO_BOTTOM_FIELD)
#define VO_PREDICTION_FLAG 4

static inline vo_frame_t * vo_get_frame (vo_instance_t * this, int flags)
{
    return this->get_frame (this, flags);
}

static inline void vo_field (vo_frame_t * frame, int flags)
{
    if (frame->field)
	frame->field (frame, flags);
}

static inline void vo_draw (vo_frame_t * frame)
{
    frame->draw (frame);
}
