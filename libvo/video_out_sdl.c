/*
 * video_out_sdl.c
 * Copyright (C) 2000 Ryan C. Gordon <icculus@lokigames.com>
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

#ifdef LIBVO_SDL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video_out.h"
#include "video_out_internal.h"

LIBVO_EXTERN(sdl)

#include "SDL.h"

static vo_info_t vo_info = 
{
	"Simple Direct Media Library (SDL)",
	"sdl",
	"Ryan C. Gordon <icculus@lokigames.com>",
	""
};

static SDL_Surface *surface = NULL;
static SDL_Overlay *overlay = NULL;
static SDL_Rect dispSize;
static Uint8 *keyState = NULL;
static int framePlaneY = -1;
static int framePlaneUV = -1;
static int slicePlaneY = -1;
static int slicePlaneUV = -1;


static inline int 
findArrayEnd(SDL_Rect **array)
/*
 * Take a null-terminated array of pointers, and find the last element.
 *
 *    params : array == array of which we want to find the last element.
 *   returns : index of last NON-NULL element.
 */
{
    int i;
    for (i = 0; array[i] != NULL; i++);  // keep loopin'...
    return(i - 1);
} // findArrayEnd

static uint32_t 
init(int width, int height, int fullscreen, char *title, uint32_t format)
/*
 * Initialize an SDL surface and an SDL YUV overlay.
 *
 *    params : width  == width of video we'll be displaying.
 *             height == height of video we'll be displaying.
 *             fullscreen == want to be fullscreen?
 *             title == Title for window titlebar.
 *   returns : non-zero on success, zero on error.
 */
{
    int rc = 0;
    int i = 0;
    const SDL_VideoInfo *vidInfo = NULL;
    int desiredWidth = -1;
    int desiredHeight = -1;
    SDL_Rect **modes = NULL;
    Uint32 sdlflags = SDL_HWSURFACE;
    Uint8 bpp;

    if (fullscreen)
        sdlflags |= SDL_FULLSCREEN;

    rc = SDL_Init(SDL_INIT_VIDEO);
    if (rc != 0)
    {
        printf("SDL: SDL_Init() failed! rc == (%d).\n", rc);
        return -1;
    } // if

    atexit(SDL_Quit);

    vidInfo = SDL_GetVideoInfo();

    modes = SDL_ListModes(vidInfo->vfmt, sdlflags);
    if (modes == NULL)
    {
        sdlflags &= ~SDL_FULLSCREEN;
        modes = SDL_ListModes(vidInfo->vfmt, sdlflags); // try without fullscreen.
        if (modes == NULL)
        {
            sdlflags &= ~SDL_HWSURFACE;
            modes = SDL_ListModes(vidInfo->vfmt, sdlflags);   // give me ANYTHING.
            if (modes == NULL)
            {
                printf("SDL: SDL_ListModes() failed.\n");
                return -1;
            } // if
        } // if
    } // if

    if (modes == (SDL_Rect **) -1)   // anything is fine.
    {
        desiredWidth = width;
        desiredHeight = height;
    } // if
    else
    {
            // we want to get the lowest resolution that'll fit the video.
            //  ...so start at the far end of the array.
        for (i = findArrayEnd(modes); ((i >= 0) && (desiredWidth == -1)); i--)
        {
            if ((modes[i]->w >= width) && (modes[i]->h >= height))
            {
                desiredWidth = modes[i]->w;
                desiredHeight = modes[i]->h;
            } // if
        } // for
    } // else

    if ((desiredWidth < 0) || (desiredHeight < 0))
    {
        printf("SDL: Couldn't produce a mode with at least"
               " a (%dx%d) resolution!\n", width, height);
        return -1;
    } // if

    dispSize.x = (desiredWidth - width) / 2;
    dispSize.y = (desiredHeight - height) / 2;
    dispSize.w = width;
    dispSize.h = height;

        // hide cursor. The cursor is annoying in fullscreen, and when
        //  using the SDL AAlib target, it tries to draw the cursor,
        //  which slows us down quite a bit.
//    if ((sdlflags & SDL_FULLSCREEN) ||
    SDL_ShowCursor(0);

        // YUV overlays need at least 16-bit color depth, but the
        //  display might less. The SDL AAlib target says it can only do
        //  8-bits, for example. So, if the display is less than 16-bits,
        //  we'll force the BPP to 16, and pray that SDL can emulate for us.
    bpp = vidInfo->vfmt->BitsPerPixel;
    if (bpp < 16)
    {
        printf("\n\n"
               "WARNING: Your SDL display target wants to be at a color\n"
               " depth of (%d), but we need it to be at least 16 bits,\n"
               " so we need to emulate 16-bit color. This is going to slow\n"
               " things down; you might want to increase your display's\n"
               " color depth, if possible.\n\n", bpp);
        bpp = 16;  // (*shrug*)
    } // if

    surface = SDL_SetVideoMode(desiredWidth, desiredHeight, bpp, sdlflags);
    if (surface == NULL)
    {
        printf("ERROR: SDL could not set the video mode!\n");
        return -1;
    } // if

    if (title == NULL)
        title = "Help! I'm trapped inside a Palm IIIc!";

    SDL_WM_SetCaption(title, "MPEG2DEC");

    overlay = SDL_CreateYUVOverlay(width, height, SDL_IYUV_OVERLAY, surface);
    if (overlay == NULL)
    {
        printf("ERROR: Couldn't create an SDL-based YUV overlay!\n");
        return -1;
    } // if

    keyState = SDL_GetKeyState(NULL);

    framePlaneY = (dispSize.w * dispSize.h);
    framePlaneUV = ((dispSize.w / 2) * (dispSize.h / 2));
    slicePlaneY = ((dispSize.w) * 16);
    slicePlaneUV = ((dispSize.w / 2) * (8));

// temp !!!!
setbuf(stdout, NULL);
    return 0;
} // display_init


static const vo_info_t*
get_info(void)
{
	return &vo_info;
}


    // !!! do we still need this API function?
static uint32_t 
draw_frame(uint8_t *src[])
/*
 * Draw a frame to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the frame.
 *  returns : non-zero on success, zero on error.
 */
{
    char *dst;

    if (SDL_LockYUVOverlay(overlay) != 0)
    {
        printf("ERROR: Couldn't lock SDL-based YUV overlay!\n");
        return(0);
    } // if

    dst = (uint8_t *) *(overlay->pixels);
    memcpy(dst, src[0], framePlaneY);
    dst += framePlaneY;
    memcpy(dst, src[1], framePlaneUV);
    dst += framePlaneUV;
    memcpy(dst, src[2], framePlaneUV);

    SDL_UnlockYUVOverlay(overlay);
    flip_page();
    return(-1);
} // display_frame


static uint32_t 
draw_slice(uint8_t *src[], int slice_num)
/*
 * Draw a slice (16 rows of image) to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the slice.
 *  returns : non-zero on success, zero on error.
 */
{
    char *dst;

    if (SDL_LockYUVOverlay(overlay) != 0)
    {
        printf("ERROR: Couldn't lock SDL-based YUV overlay!\n");
        return(0);
    } // if

    dst = (uint8_t *) *(overlay->pixels) + (slicePlaneY * slice_num);
    memcpy(dst, src[0], slicePlaneY);
    dst = ((uint8_t *) *(overlay->pixels) + framePlaneY) +
	(slicePlaneUV * slice_num);
    memcpy(dst, src[1], slicePlaneUV);
    dst += framePlaneUV;
    memcpy(dst, src[2], slicePlaneUV);

    SDL_UnlockYUVOverlay(overlay);
    return(-1);
} // display_slice


static void 
flip_page(void)
{
    SDL_PumpEvents();  // get keyboard and win resize events.
    if ( (SDL_GetModState() & KMOD_ALT) &&
         ((keyState[SDLK_KP_ENTER] == SDL_PRESSED) ||
          (keyState[SDLK_RETURN] == SDL_PRESSED)) )
    {
        SDL_WM_ToggleFullScreen(surface);
    } // if

    SDL_DisplayYUVOverlay(overlay, &dispSize);
} // display_flip_page

static vo_image_buffer_t* 
allocate_image_buffer()
{
	//use the generic fallback
	return allocate_image_buffer_common(dispSize.h,dispSize.w,0x32315659);
}

static void	
free_image_buffer(vo_image_buffer_t* image)
{
	//use the generic fallback
	free_image_buffer_common(image);
}

#endif
