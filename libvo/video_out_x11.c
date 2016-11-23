#define DISP

/*
 * video_out_x11.c
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
 *
 * Some of the code in this file is derived from MSSG code :
 * Copyright (C) 1996, MPEG Software Simulation Group. All Rights Reserved.
 */

#include "config.h"

#ifdef LIBVO_X11

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

#include "video_out.h"
#include "video_out_internal.h"
#include "yuv2rgb.h"

// not defined on solaris 2.6... grumbl
Bool XShmQueryExtension (Display *);

LIBVO_EXTERN(x11)

static vo_info_t vo_info = 
{
#ifdef LIBVO_XV
	"X11 (Xv)",
#else
	"X11",
#endif
	"x11",
	"Aaron Holtzman <aholtzma@ess.engr.uvic.ca>",
	""
};

/* private prototypes */
static void Display_Image (XImage * myximage, unsigned char *ImageData);

/* since it doesn't seem to be defined on some platforms */
int XShmGetEventBase(Display*);

/* local data */
static unsigned char *ImageData;

/* X11 related variables */
static Display *mydisplay;
static Window mywindow;
static GC mygc;
static XImage *myximage;
static int depth, bpp, mode;
static XWindowAttributes attribs;
static int X_already_started = 0;

#ifdef LIBVO_XV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
// FIXME: dynamically allocate this stuff
#define MAX_BUFFERS 7
static int xvimage_counter = 0;
static int current_image = 0;
static void allocate_xvimage(int);
static unsigned int ver,rel,req,ev,err;
static unsigned int formats, adaptors,i,xv_format;
static XvPortID xv_port;
static int win_width,win_height;
static XvAdaptorInfo        *ai;
static XvImageFormatValues  *fo;
static XvImage *xvimage[MAX_BUFFERS];
#endif

#define SH_MEM

#ifdef SH_MEM

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

//static int HandleXError _ANSI_ARGS_((Display * dpy, XErrorEvent * event));
static void InstallXErrorHandler (void);
static void DeInstallXErrorHandler (void);

static int Shmem_Flag;
static int Quiet_Flag;
#ifdef LIBVO_XV
static XShmSegmentInfo Shminfo[MAX_BUFFERS];
#else
static XShmSegmentInfo Shminfo[1];
#endif
static int gXErrorFlag;
static int CompletionType = -1;

static void InstallXErrorHandler()
{
	//XSetErrorHandler(HandleXError);
	XFlush(mydisplay);
}

static void DeInstallXErrorHandler()
{
	XSetErrorHandler(NULL);
	XFlush(mydisplay);
}

#endif

static uint32_t image_width;
static uint32_t image_height;

/* connect to server, create and map window,
 * allocate colors and (shared) memory
 */
static uint32_t 
init(int width, int height, int fullscreen, char *title, uint32_t format)
{
	int screen;
	unsigned int fg, bg;
	char *hello = (title == NULL) ? "I hate X11" : title;
	char *name = ":0.0";
	XSizeHints hint;
	XVisualInfo vinfo;
	XEvent xev;

	XGCValues xgcv;
	Colormap theCmap;
	XSetWindowAttributes xswa;
	unsigned long xswamask;

	image_height = height;
	image_width = width;

	if (X_already_started)
		return -1;

	if(getenv("DISPLAY"))
		name = getenv("DISPLAY");

	mydisplay = XOpenDisplay(name);

	if (mydisplay == NULL)
	{
		fprintf(stderr,"Can not open display\n");
		return -1;
	}

	screen = DefaultScreen(mydisplay);

	hint.x = 0;
	hint.y = 0;
	hint.width = image_width;
	hint.height = image_height;
	hint.flags = PPosition | PSize;

	/* Get some colors */

	bg = WhitePixel(mydisplay, screen);
	fg = BlackPixel(mydisplay, screen);

	/* Make the window */

	XGetWindowAttributes(mydisplay, DefaultRootWindow(mydisplay), &attribs);

	/*
	 *
	 * depth in X11 terminology land is the number of bits used to
	 * actually represent the colour.
   	 *
	 * bpp in X11 land means how many bits in the frame buffer per
	 * pixel. 
	 *
	 * ex. 15 bit color is 15 bit depth and 16 bpp. Also 24 bit
	 *     color is 24 bit depth, but can be 24 bpp or 32 bpp.
	 */

	depth = attribs.depth;

	if (depth != 15 && depth != 16 && depth != 24 && depth != 32) 
	{
		/* The root window may be 8bit but there might still be
		* visuals with other bit depths. For example this is the 
		* case on Sun/Solaris machines.
		*/
		depth = 24;
	}
	//BEGIN HACK
	//mywindow = XCreateSimpleWindow(mydisplay, DefaultRootWindow(mydisplay),
	//hint.x, hint.y, hint.width, hint.height, 4, fg, bg);
	//
	XMatchVisualInfo(mydisplay, screen, depth, TrueColor, &vinfo);

	theCmap   = XCreateColormap(mydisplay, RootWindow(mydisplay,screen), 
	vinfo.visual, AllocNone);

	xswa.background_pixel = 0;
	xswa.border_pixel     = 1;
	xswa.colormap         = theCmap;
	xswamask = CWBackPixel | CWBorderPixel |CWColormap;


	mywindow = XCreateWindow(mydisplay, RootWindow(mydisplay,screen),
	hint.x, hint.y, hint.width, hint.height, 4, depth,CopyFromParent,vinfo.visual,xswamask,&xswa);

	XSelectInput(mydisplay, mywindow, StructureNotifyMask);

	/* Tell other applications about this window */

	XSetStandardProperties(mydisplay, mywindow, hello, hello, None, NULL, 0, &hint);

	/* Map window. */

	XMapWindow(mydisplay, mywindow);

	/* Wait for map. */
	do 
	{
		XNextEvent(mydisplay, &xev);
	}
	while (xev.type != MapNotify || xev.xmap.event != mywindow);

	XSelectInput(mydisplay, mywindow, NoEventMask);

	XFlush(mydisplay);
	XSync(mydisplay, False);

	mygc = XCreateGC(mydisplay, mywindow, 0L, &xgcv);

#ifdef LIBVO_XV
	xv_port = 0;
	if (Success == XvQueryExtension(mydisplay,&ver,&rel,&req,&ev,&err)) 
	{
		/* check for Xvideo support */
		if (Success != XvQueryAdaptors(mydisplay,DefaultRootWindow(mydisplay), &adaptors,&ai)) 
		{
			fprintf(stderr,"Xv: XvQueryAdaptors failed");
			return -1;
		}
		/* check adaptors */
		for (i = 0; i < adaptors; i++) 
		{
			if ((ai[i].type & XvInputMask) && (ai[i].type & XvImageMask) && (xv_port == 0)) 
				xv_port = ai[i].base_id;
		}
		/* check image formats */
		if (xv_port != 0) 
		{
			fo = XvListImageFormats(mydisplay, xv_port, (int*)&formats);

			for(i = 0; i < formats; i++) 
			{
				fprintf(stderr, "Xvideo image format: 0x%x (%4.4s) %s\n", fo[i].id, 
						(char*)&fo[i].id, (fo[i].format == XvPacked) ? "packed" : "planar");

				if (fo[i].id == format) 
				{
					xv_format = fo[i].id;
					break;
				}
			}
			if (i == formats) /* no matching image format not */
				xv_port = 0;
		}

		if (xv_port != 0) 
		{
			fprintf(stderr,"using Xvideo port %ld for hw scaling\n",
			xv_port);

			allocate_xvimage(0);

			/* catch window resizes */
			XSelectInput(mydisplay, mywindow, StructureNotifyMask);
			win_width  = image_width;
			win_height = image_height;

			/* all done (I hope...) */
			X_already_started++;
			return 0;
		}
	}
#endif

#ifdef SH_MEM
	if (XShmQueryExtension(mydisplay))
		Shmem_Flag = 1;
	else 
	{
		Shmem_Flag = 0;
		if (!Quiet_Flag)
			fprintf(stderr, "Shared memory not supported\nReverting to normal Xlib\n");
	}
	if (Shmem_Flag)
		CompletionType = XShmGetEventBase(mydisplay) + ShmCompletion;

	InstallXErrorHandler();

	if (Shmem_Flag) 
	{
		myximage = XShmCreateImage(mydisplay, vinfo.visual, 
		depth, ZPixmap, NULL, &Shminfo[0], width, image_height);

		/* If no go, then revert to normal Xlib calls. */

		if (myximage == NULL ) 
		{
			if (myximage != NULL)
				XDestroyImage(myximage);
			if (!Quiet_Flag)
				fprintf(stderr, "Shared memory error, disabling (Ximage error)\n");

			goto shmemerror;
		}
		/* Success here, continue. */

		Shminfo[0].shmid = shmget(IPC_PRIVATE, 
		myximage->bytes_per_line * myximage->height ,
		IPC_CREAT | 0777);
		if (Shminfo[0].shmid < 0 ) 
		{
			XDestroyImage(myximage);
			if (!Quiet_Flag)
			{
				printf("%s\n",strerror(errno));
				perror(strerror(errno));
				fprintf(stderr, "Shared memory error, disabling (seg id error)\n");
			}
			goto shmemerror;
		}
		Shminfo[0].shmaddr = (char *) shmat(Shminfo[0].shmid, 0, 0);

		if (Shminfo[0].shmaddr == ((char *) -1)) 
		{
			XDestroyImage(myximage);
			if (Shminfo[0].shmaddr != ((char *) -1))
				shmdt(Shminfo[0].shmaddr);
			if (!Quiet_Flag) 
				fprintf(stderr, "Shared memory error, disabling (address error)\n");
			goto shmemerror;
		}
		myximage->data = Shminfo[0].shmaddr;
		ImageData = (unsigned char *) myximage->data;
		Shminfo[0].readOnly = False;
		XShmAttach(mydisplay, &Shminfo[0]);

		XSync(mydisplay, False);

		if (gXErrorFlag) 
		{
			/* Ultimate failure here. */
			XDestroyImage(myximage);
			shmdt(Shminfo[0].shmaddr);
			if (!Quiet_Flag)
				fprintf(stderr, "Shared memory error, disabling.\n");
			gXErrorFlag = 0;
			goto shmemerror;
		} 
		else 
		{
			shmctl(Shminfo[0].shmid, IPC_RMID, 0);
		}

		if (!Quiet_Flag) 
		{
			fprintf(stderr, "Sharing memory.\n");
		}
	} 
	else 
	{
		shmemerror:
		Shmem_Flag = 0;
#endif
		myximage = XGetImage(mydisplay, mywindow, 0, 0,
		width, image_height, AllPlanes, ZPixmap);
		ImageData = myximage->data;
#ifdef SH_MEM
	}

	DeInstallXErrorHandler();
#endif

	bpp = myximage->bits_per_pixel;

	// If we have blue in the lowest bit then obviously RGB 
	mode = ((myximage->blue_mask & 0x01) != 0) ? MODE_RGB : MODE_BGR;
#ifdef WORDS_BIGENDIAN 
	if (myximage->byte_order != MSBFirst)
#else
	if (myximage->byte_order != LSBFirst) 
#endif
	{
		fprintf( stderr, "No support fon non-native XImage byte order!\n" );
		return -1;
	}

	/* 
	 * If depth is 24 then it may either be a 3 or 4 byte per pixel
	 * format. We can't use bpp because then we would lose the 
	 * distinction between 15/16bit depth (2 byte formate assumed).
	 *
	 * FIXME - change yuv2rgb_init to take both depth and bpp
	 * parameters
	 */
	yuv2rgb_init((depth == 24) ? bpp : depth, mode);

	X_already_started++;
	return 0;
}

static const vo_info_t*
get_info(void)
{
	return &vo_info;
}

#ifdef LIBVO_XV
static void
allocate_xvimage(int foo)
{
	/* allocate XvImages.  FIXME: no error checking, without
	 * mit-shm this will bomb... */
	xvimage[foo] = XvShmCreateImage(mydisplay, xv_port, xv_format, 0, image_width, image_height, &Shminfo[foo]);
			
	Shminfo[foo].shmid    = shmget(IPC_PRIVATE, xvimage[foo]->data_size, IPC_CREAT | 0777);
	Shminfo[foo].shmaddr  = (char *) shmat(Shminfo[foo].shmid, 0, 0);
	Shminfo[foo].readOnly = False;
			
	xvimage[foo]->data = Shminfo[foo].shmaddr;
	XShmAttach(mydisplay, &Shminfo[foo]);
	XSync(mydisplay, False);
	shmctl(Shminfo[foo].shmid, IPC_RMID, 0);

	/* so we can do grayscale while testing... */
	memset(xvimage[foo]->data,128,xvimage[foo]->data_size);

	return;
}
#endif

#if 0
static void 
Terminate_Display_Process(void) 
{
	getchar();	/* wait for enter to remove window */
#ifdef SH_MEM
	if (Shmem_Flag) 
	{
		XShmDetach(mydisplay, &Shminfo[0]);
		XDestroyImage(myximage);
		shmdt(Shminfo[0].shmaddr);
	}
#endif
	XDestroyWindow(mydisplay, mywindow);
	XCloseDisplay(mydisplay);
	X_already_started = 0;
}
#endif

static void 
Display_Image(XImage *myximage, uint8_t *ImageData)
{
#ifdef DISP
#ifdef SH_MEM
	if (Shmem_Flag) 
	{
		XShmPutImage(mydisplay, mywindow, mygc, myximage, 
				0, 0, 0, 0, myximage->width, myximage->height, True); 
		XFlush(mydisplay);
	} 
	else
#endif
	{
		XPutImage(mydisplay, mywindow, mygc, myximage, 0, 0, 0, 0, 
				myximage->width, myximage->height);
		XFlush(mydisplay);
	}
#endif
}

#ifdef LIBVO_XV
static void
check_events(void)
{
	Window root;
	XEvent event;
	int x, y;
	unsigned int w, h, b, d;

	if (XCheckWindowEvent(mydisplay, mywindow, StructureNotifyMask, &event))
	{
		XGetGeometry(mydisplay, mywindow, &root, &x, &y, &w, &h, &b, &d);
		win_width  = w;
		win_height = h;
	}
}
#endif

#ifdef LIBVO_XV
static inline void
flip_page_xv(void)
{
	check_events();

#ifdef DISP
	XvShmPutImage(mydisplay, xv_port, mywindow, mygc, xvimage[current_image],
		0, 0,  image_width, image_height,
		0, 0,  win_width, win_height,
		False);
	XFlush(mydisplay);
#endif
	return;
}
#endif

static inline void
flip_page_x11(void)
{
	Display_Image(myximage, ImageData);
}


static void
flip_page(void)
{
#ifdef LIBVO_XV
	if (xv_port != 0)
		return flip_page_xv();
	else
#endif
		return flip_page_x11();
}

#ifdef LIBVO_XV
static inline uint32_t
draw_slice_xv(uint8_t *src[], int slice_num)
{
	uint8_t *dst;

	dst = xvimage[0]->data + image_width * 16 * slice_num;

	memcpy(dst,src[0],image_width*16);
	dst = xvimage[0]->data + image_width * image_height + image_width * 4 * slice_num;
	memcpy(dst, src[2],image_width*4);
	dst = xvimage[0]->data + image_width * image_height * 5 / 4 + image_width * 4 * slice_num;
	memcpy(dst, src[1],image_width*4);

	return 0;  
}
#endif

static inline uint32_t
draw_slice_x11(uint8_t *src[], int slice_num)
{
	uint8_t *dst;

	dst = ImageData + image_width * 16 * (bpp/8) * slice_num;

	yuv2rgb(dst , src[0], src[1], src[2], 
			image_width, 16, 
			image_width*(bpp/8), image_width, image_width/2 );
	return 0;
}

static uint32_t
draw_slice(uint8_t *src[], int slice_num)
{
#ifdef LIBVO_XV
	if (xv_port != 0)
		return draw_slice_xv(src,slice_num);
	else
#endif
		return draw_slice_x11(src,slice_num);
}

#ifdef LIBVO_XV
static inline uint32_t 
draw_frame_xv(uint8_t *src[])
{
	int foo;

	check_events();

	for(foo = xvimage_counter ; foo > 0 ; foo--)
		if (src[0] == (uint8_t*) xvimage[foo]->data)
		{
			current_image = foo;
			return 0;
		}
	
	memcpy(xvimage[0]->data,src[0],image_width*image_height);
	memcpy(xvimage[0]->data+image_width*image_height,src[2],image_width*image_height/4);
	memcpy(xvimage[0]->data+image_width*image_height*5/4,src[1],image_width*image_height/4);
	
	current_image = 0;
	
	return 0;  
}
#endif

static inline uint32_t 
draw_frame_x11(uint8_t *src[])
{
	yuv2rgb(ImageData, src[0], src[1], src[2],
		image_width, image_height, 
		image_width*(bpp/8), image_width, image_width/2 );

	Display_Image(myximage, ImageData);
	return 0; 
}

static uint32_t
draw_frame(uint8_t *src[])
{
#ifdef LIBVO_XV
	if (xv_port != 0)
		return draw_frame_xv(src);
	else
#endif
		return draw_frame_x11(src);
}

static vo_image_buffer_t* 
allocate_image_buffer()
{
#ifdef LIBVO_XV
	xvimage_counter++;
	
	if ((xv_port != 0) && (xvimage_counter < MAX_BUFFERS))
	{
		vo_image_buffer_t *image;

		image = malloc(sizeof(vo_image_buffer_t));

		if (!image) return NULL;
		
		allocate_xvimage(xvimage_counter);

		image->base = xvimage[xvimage_counter]->data;
		image->height = image_height;
		image->width = image_width;
		
		return image;
	}
	else
#endif
	{
		//use the generic fallback
		return allocate_image_buffer_common(image_height, image_width, 0x32315659);
	}
}

static void	
free_image_buffer(vo_image_buffer_t* image)
{
#ifdef LIBVO_XV
	if (xv_port != 0)
	{
		// FIXME: properly deallocate XvImages
	}
	else
#endif
	{
		//use the generic fallback
		free_image_buffer_common(image);
	}
}

#endif
