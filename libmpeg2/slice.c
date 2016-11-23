/*
 * slice.c
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
#include "mpeg2.h"
#include "mpeg2_internal.h"

#include "motion_comp.h"
#include "vlc.h"
#include "idct.h"

extern mc_functions_t mc_functions;
extern void (* idct_block_copy) (int16_t * block, uint8_t * dest, int stride);
extern void (* idct_block_add) (int16_t * block, uint8_t * dest, int stride);

//XXX put these on the stack in slice_process?
static slice_t slice;
static int16_t DCTblock[64] ALIGN_16_BYTE;

static uint32_t bitstream_buf;
static int bitstream_bits;
static uint8_t * bitstream_ptr;

static int non_linear_quantizer_scale [] = {
     0,  1,  2,  3,  4,  5,   6,   7,
     8, 10, 12, 14, 16, 18,  20,  22,
    24, 28, 32, 36, 40, 44,  48,  52,
    56, 64, 72, 80, 88, 96, 104, 112
};

static inline int get_macroblock_modes (int picture_coding_type,
					int frame_pred_frame_dct)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits
    int macroblock_modes;
    MBtab * tab;

    switch (picture_coding_type) {
    case I_TYPE:

	tab = MB_I + UBITS (bit_buf, 1);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if (! frame_pred_frame_dct) {
	    macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
	    DUMPBITS (bit_buf, bits, 1);
	}

	return macroblock_modes;

    case P_TYPE:

	tab = MB_P + UBITS (bit_buf, 5);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if (frame_pred_frame_dct) {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
		macroblock_modes |= MC_FRAME;
	    return macroblock_modes;
	} else {
	    if (macroblock_modes & MACROBLOCK_MOTION_FORWARD) {
		macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
		DUMPBITS (bit_buf, bits, 2);
	    }
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
		macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
		DUMPBITS (bit_buf, bits, 1);
	    }
	    return macroblock_modes;
	}

    case B_TYPE:

	tab = MB_B + UBITS (bit_buf, 6);
	DUMPBITS (bit_buf, bits, tab->len);
	macroblock_modes = tab->modes;

	if (frame_pred_frame_dct) {
	    //if (! (macroblock_modes & MACROBLOCK_INTRA))
	    macroblock_modes |= MC_FRAME;
	    return macroblock_modes;
	} else {
	    if (macroblock_modes & MACROBLOCK_INTRA)
		goto intra;
	    macroblock_modes |= UBITS (bit_buf, 2) * MOTION_TYPE_BASE;
	    DUMPBITS (bit_buf, bits, 2);
	    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN)) {
	    intra:
		macroblock_modes |= UBITS (bit_buf, 1) * DCT_TYPE_INTERLACED;
		DUMPBITS (bit_buf, bits, 1);
	    }
	    return macroblock_modes;
	}

    case D_TYPE:

	DUMPBITS (bit_buf, bits, 1);
	return MACROBLOCK_INTRA;

    default:
	return 0;
    }
#undef bit_buf
#undef bits
}

static inline int get_quantizer_scale (int q_scale_type)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits

    int quantizer_scale_code;

    quantizer_scale_code = UBITS (bit_buf, 5);
    DUMPBITS (bit_buf, bits, 5);

    if (q_scale_type)
	return non_linear_quantizer_scale [quantizer_scale_code];
    else
	return quantizer_scale_code << 1;
#undef bit_buf
#undef bits
}

static inline int get_motion_delta (int f_code)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits

    int delta;
    int sign;
    MVtab * tab;

    if (bit_buf & 0x80000000) {
	DUMPBITS (bit_buf, bits, 1);
	return 0;
    } else if (bit_buf >= 0x0c000000) {

	tab = MV_4 + UBITS (bit_buf, 4);
	delta = (tab->delta << f_code) + 1;
	bits += tab->len + f_code + 1;
	bit_buf <<= tab->len;

	sign = SBITS (bit_buf, 1);
	bit_buf <<= 1;

	if (f_code)
	    delta += UBITS (bit_buf, f_code);
	bit_buf <<= f_code;

	return (delta ^ sign) - sign;

    } else {

	tab = MV_10 + UBITS (bit_buf, 10);
	delta = (tab->delta << f_code) + 1;
	bits += tab->len + 1;
	bit_buf <<= tab->len;

	sign = SBITS (bit_buf, 1);
	bit_buf <<= 1;

	if (f_code) {
	    NEEDBITS (bit_buf, bits);
	    delta += UBITS (bit_buf, f_code);
	    DUMPBITS (bit_buf, bits, f_code);
	}

	return (delta ^ sign) - sign;

    }
#undef bit_buf
#undef bits
}

static inline int bound_motion_vector (int vector, int f_code)
{
#if 1
    int limit;

    limit = 16 << f_code;

    if (vector >= limit)
	return vector - 2*limit;
    else if (vector < -limit)
	return vector + 2*limit;
    else return vector;
#else
    return (vector << (27 - f_code)) >> (27 - f_code);
#endif
}

static inline int get_dmv (void)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits

    DMVtab * tab;

    tab = DMV_2 + UBITS (bit_buf, 2);
    DUMPBITS (bit_buf, bits, tab->len);
    return tab->dmv;
#undef bit_buf
#undef bits
}

static inline int get_coded_block_pattern (void)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits

    CBPtab * tab;

    NEEDBITS (bit_buf, bits);

    if (bit_buf >= 0x20000000) {

	tab = CBP_7 - 16 + UBITS (bit_buf, 7);
	DUMPBITS (bit_buf, bits, tab->len);
	return tab->cbp;

    } else {

	tab = CBP_9 + UBITS (bit_buf, 9);
	DUMPBITS (bit_buf, bits, tab->len);
	return tab->cbp;
    }

#undef bit_buf
#undef bits
}

static inline int get_luma_dc_dct_diff (void)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits
    DCtab * tab;
    int size;
    int dc_diff;

    if (bit_buf < 0xf8000000) {
	tab = DC_lum_5 + UBITS (bit_buf, 5);
	size = tab->size;
	if (size) {
	    bits += tab->len + size;
	    bit_buf <<= tab->len;
	    dc_diff =
		UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	    bit_buf <<= size;
	    return dc_diff;
	} else {
	    DUMPBITS (bit_buf, bits, 3);
	    return 0;
	}
    } else {
	tab = DC_long - 0x1e0 + UBITS (bit_buf, 9);
	size = tab->size;
	DUMPBITS (bit_buf, bits, tab->len);
	NEEDBITS (bit_buf, bits);
	dc_diff = UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	DUMPBITS (bit_buf, bits, size);
	return dc_diff;
    }
#undef bit_buf
#undef bits
}

static inline int get_chroma_dc_dct_diff (void)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits
    DCtab * tab;
    int size;
    int dc_diff;

    if (bit_buf < 0xf8000000) {
	tab = DC_chrom_5 + UBITS (bit_buf, 5);
	size = tab->size;
	if (size) {
	    bits += tab->len + size;
	    bit_buf <<= tab->len;
	    dc_diff =
		UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	    bit_buf <<= size;
	    return dc_diff;
	} else {
	    DUMPBITS (bit_buf, bits, 2);
	    return 0;
	}
    } else {
	tab = DC_long - 0x3e0 + UBITS (bit_buf, 10);
	size = tab->size;
	DUMPBITS (bit_buf, bits, tab->len + 1);
	NEEDBITS (bit_buf, bits);
	dc_diff = UBITS (bit_buf, size) - UBITS (SBITS (~bit_buf, 1), size);
	DUMPBITS (bit_buf, bits, size);
	return dc_diff;
    }
#undef bit_buf
#undef bits
}

#define SATURATE(val)		\
do {				\
    if (val > 2047)		\
	val = 2047;		\
    else if (val < -2048)	\
	val = -2048;		\
} while (0)

static void get_intra_block_B14 (picture_t * picture, slice_t * slice,
				 int16_t * dest)
{
    int i;
    int j;
    int val;
    uint8_t * scan = picture->scan;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = slice->quantizer_scale;
    int mismatch;
    DCTtab * tab;
    uint32_t bit_buf;
    int bits;

    i = 0;
    mismatch = ~dest[0];

    bit_buf = bitstream_buf;
    bits = bitstream_bits;

    NEEDBITS (bit_buf, bits);

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 - 5 + UBITS (bit_buf, 5);

	    i += tab->run;
	    if (i >= 64)
		break;	// end of block

	normal_code:
	    j = scan[i];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = (tab->level * quantizer_scale * quant_matrix[j]) >> 4;

	    // if (bitstream_get (1)) val = -val;
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 - 4 + UBITS (bit_buf, 8);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    // escape code

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	// illegal, but check needed to avoid buffer overflow

	    j = scan[i];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);
	    val = (SBITS (bit_buf, 12) *
		   quantizer_scale * quant_matrix[j]) / 16;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 - 8 + UBITS (bit_buf, 10);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 - 16 + UBITS (bit_buf, 13);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 - 16 + UBITS (bit_buf, 15);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    bit_buf |= getword () << (bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	// illegal, but check needed to avoid buffer overflow
    }
    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 2);	// dump end of block code
    bitstream_buf = bit_buf;
    bitstream_bits = bits;
}

static void get_intra_block_B15 (picture_t * picture, slice_t * slice,
				 int16_t * dest)
{
    int i;
    int j;
    int val;
    uint8_t * scan = picture->scan;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = slice->quantizer_scale;
    int mismatch;
    DCTtab * tab;
    uint32_t bit_buf;
    int bits;

    i = 0;
    mismatch = ~dest[0];

    bit_buf = bitstream_buf;
    bits = bitstream_bits;

    NEEDBITS (bit_buf, bits);

    while (1) {
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B15_8 - 4 + UBITS (bit_buf, 8);

	    i += tab->run;
	    if (i < 64) {

	    normal_code:
		j = scan[i];
		bit_buf <<= tab->len;
		bits += tab->len + 1;
		val = (tab->level * quantizer_scale * quant_matrix[j]) >> 4;

		// if (bitstream_get (1)) val = -val;
		val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		bit_buf <<= 1;
		NEEDBITS (bit_buf, bits);

		continue;

	    } else {

		// end of block. I commented out this code because if we
		// dont exit here we will still exit at the later test :)

		//if (i >= 128) break;	// end of block

		// escape code

		i += UBITS (bit_buf << 6, 6) - 64;
		if (i >= 64)
		    break;	// illegal, but check against buffer overflow

		j = scan[i];

		DUMPBITS (bit_buf, bits, 12);
		NEEDBITS (bit_buf, bits);
		val = (SBITS (bit_buf, 12) *
		       quantizer_scale * quant_matrix[j]) / 16;

		SATURATE (val);
		dest[j] = val;
		mismatch ^= val;

		DUMPBITS (bit_buf, bits, 12);
		NEEDBITS (bit_buf, bits);

		continue;

	    }
	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B15_10 - 8 + UBITS (bit_buf, 10);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 - 16 + UBITS (bit_buf, 13);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 - 16 + UBITS (bit_buf, 15);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    bit_buf |= getword () << (bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	// illegal, but check needed to avoid buffer overflow
    }
    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 4);	// dump end of block code
    bitstream_buf = bit_buf;
    bitstream_bits = bits;
}

static void get_non_intra_block (picture_t * picture, slice_t * slice,
				 int16_t * dest)
{
    int i;
    int j;
    int val;
    uint8_t * scan = picture->scan;
    uint8_t * quant_matrix = picture->non_intra_quantizer_matrix;
    int quantizer_scale = slice->quantizer_scale;
    int mismatch;
    DCTtab * tab;
    uint32_t bit_buf;
    int bits;

    i = -1;
    mismatch = 1;

    bit_buf = bitstream_buf;
    bits = bitstream_bits;

    NEEDBITS (bit_buf, bits);
    if (bit_buf >= 0x28000000) {
	tab = DCT_B14DC_5 - 5 + UBITS (bit_buf, 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 - 5 + UBITS (bit_buf, 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	// end of block

	normal_code:
	    j = scan[i];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = ((2*tab->level+1) * quantizer_scale * quant_matrix[j]) >> 5;

	    // if (bitstream_get (1)) val = -val;
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits);

	    continue;

	}

    entry_2:
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 - 4 + UBITS (bit_buf, 8);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    // escape code

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	// illegal, but check needed to avoid buffer overflow

	    j = scan[i];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);
	    val = 2 * (SBITS (bit_buf, 12) + SBITS (bit_buf, 1)) + 1;
	    val = (val * quantizer_scale * quant_matrix[j]) / 32;

	    SATURATE (val);
	    dest[j] = val;
	    mismatch ^= val;

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 - 8 + UBITS (bit_buf, 10);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 - 16 + UBITS (bit_buf, 13);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 - 16 + UBITS (bit_buf, 15);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    bit_buf |= getword () << (bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	// illegal, but check needed to avoid buffer overflow
    }
    dest[63] ^= mismatch & 1;
    DUMPBITS (bit_buf, bits, 2);	// dump end of block code
    bitstream_buf = bit_buf;
    bitstream_bits = bits;
}

static void get_mpeg1_intra_block (picture_t * picture, slice_t * slice,
				   int16_t * dest)
{
    int i;
    int j;
    int val;
    uint8_t * scan = picture->scan;
    uint8_t * quant_matrix = picture->intra_quantizer_matrix;
    int quantizer_scale = slice->quantizer_scale;
    DCTtab * tab;
    uint32_t bit_buf;
    int bits;

    i = 0;

    bit_buf = bitstream_buf;
    bits = bitstream_bits;

    NEEDBITS (bit_buf, bits);

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 - 5 + UBITS (bit_buf, 5);

	    i += tab->run;
	    if (i >= 64)
		break;	// end of block

	normal_code:
	    j = scan[i];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = (tab->level * quantizer_scale * quant_matrix[j]) >> 4;

	    // oddification
	    val = (val - 1) | 1;

	    // if (bitstream_get (1)) val = -val;
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 - 4 + UBITS (bit_buf, 8);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    // escape code

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	// illegal, but check needed to avoid buffer overflow

	    j = scan[i];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);
	    val = SBITS (bit_buf, 8);
	    if (! (val & 0x7f)) {
		DUMPBITS (bit_buf, bits, 8);
		val = UBITS (bit_buf, 8) + 2 * val;
	    }
	    val = (val * quantizer_scale * quant_matrix[j]) / 16;

	    // oddification
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    DUMPBITS (bit_buf, bits, 8);
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 - 8 + UBITS (bit_buf, 10);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 - 16 + UBITS (bit_buf, 13);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 - 16 + UBITS (bit_buf, 15);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    bit_buf |= getword () << (bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	// illegal, but check needed to avoid buffer overflow
    }
    DUMPBITS (bit_buf, bits, 2);	// dump end of block code
    bitstream_buf = bit_buf;
    bitstream_bits = bits;
}

static void get_mpeg1_non_intra_block (picture_t * picture, slice_t * slice,
				       int16_t * dest)
{
    int i;
    int j;
    int val;
    uint8_t * scan = picture->scan;
    uint8_t * quant_matrix = picture->non_intra_quantizer_matrix;
    int quantizer_scale = slice->quantizer_scale;
    DCTtab * tab;
    uint32_t bit_buf;
    int bits;

    i = -1;

    bit_buf = bitstream_buf;
    bits = bitstream_bits;

    NEEDBITS (bit_buf, bits);
    if (bit_buf >= 0x28000000) {
	tab = DCT_B14DC_5 - 5 + UBITS (bit_buf, 5);
	goto entry_1;
    } else
	goto entry_2;

    while (1) {
	if (bit_buf >= 0x28000000) {

	    tab = DCT_B14AC_5 - 5 + UBITS (bit_buf, 5);

	entry_1:
	    i += tab->run;
	    if (i >= 64)
		break;	// end of block

	normal_code:
	    j = scan[i];
	    bit_buf <<= tab->len;
	    bits += tab->len + 1;
	    val = ((2*tab->level+1) * quantizer_scale * quant_matrix[j]) >> 5;

	    // oddification
	    val = (val - 1) | 1;

	    // if (bitstream_get (1)) val = -val;
	    val = (val ^ SBITS (bit_buf, 1)) - SBITS (bit_buf, 1);

	    SATURATE (val);
	    dest[j] = val;

	    bit_buf <<= 1;
	    NEEDBITS (bit_buf, bits);

	    continue;

	}

    entry_2:
	if (bit_buf >= 0x04000000) {

	    tab = DCT_B14_8 - 4 + UBITS (bit_buf, 8);

	    i += tab->run;
	    if (i < 64)
		goto normal_code;

	    // escape code

	    i += UBITS (bit_buf << 6, 6) - 64;
	    if (i >= 64)
		break;	// illegal, but check needed to avoid buffer overflow

	    j = scan[i];

	    DUMPBITS (bit_buf, bits, 12);
	    NEEDBITS (bit_buf, bits);
	    val = SBITS (bit_buf, 8);
	    if (! (val & 0x7f)) {
		DUMPBITS (bit_buf, bits, 8);
		val = UBITS (bit_buf, 8) + 2 * val;
	    }
	    val = 2 * (val + SBITS (val, 1)) + 1;
	    val = (val * quantizer_scale * quant_matrix[j]) / 32;

	    // oddification
	    val = (val + ~SBITS (val, 1)) | 1;

	    SATURATE (val);
	    dest[j] = val;

	    DUMPBITS (bit_buf, bits, 8);
	    NEEDBITS (bit_buf, bits);

	    continue;

	} else if (bit_buf >= 0x02000000) {
	    tab = DCT_B14_10 - 8 + UBITS (bit_buf, 10);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00800000) {
	    tab = DCT_13 - 16 + UBITS (bit_buf, 13);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else if (bit_buf >= 0x00200000) {
	    tab = DCT_15 - 16 + UBITS (bit_buf, 15);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	} else {
	    tab = DCT_16 + UBITS (bit_buf, 16);
	    bit_buf <<= 16;
	    bit_buf |= getword () << (bits + 16);
	    i += tab->run;
	    if (i < 64)
		goto normal_code;
	}
	break;	// illegal, but check needed to avoid buffer overflow
    }
    DUMPBITS (bit_buf, bits, 2);	// dump end of block code
    bitstream_buf = bit_buf;
    bitstream_bits = bits;
}

static inline int get_macroblock_address_increment (void)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits

    MBAtab * tab;
    int mba;

    mba = 0;

    while (1) {
	if (bit_buf >= 0x10000000) {
	    tab = MBA_5 - 2 + UBITS (bit_buf, 5);
	    DUMPBITS (bit_buf, bits, tab->len);
	    return mba + tab->mba;
	} else if (bit_buf >= 0x03000000) {
	    tab = MBA_11 - 24 + UBITS (bit_buf, 11);
	    DUMPBITS (bit_buf, bits, tab->len);
	    return mba + tab->mba;
	} else switch (UBITS (bit_buf, 11)) {
	case 8:		// macroblock_escape
	    mba += 33;
	    // no break here on purpose
	case 15:	// macroblock_stuffing (MPEG1 only)
	    DUMPBITS (bit_buf, bits, 11);
	    NEEDBITS (bit_buf, bits);
	    break;
	default:	// end of slice, or error
	    return 0;
	}
    }

#undef bit_buf
#undef bits
}

static inline void slice_intra_DCT (picture_t * picture, slice_t * slice,
				    int cc, uint8_t * dest, int stride)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits
    NEEDBITS (bit_buf, bits);
    //Get the intra DC coefficient and inverse quantize it
    if (cc == 0)
	slice->dc_dct_pred[0] += get_luma_dc_dct_diff ();
    else
	slice->dc_dct_pred[cc] += get_chroma_dc_dct_diff ();
    DCTblock[0] = slice->dc_dct_pred[cc] << (3 - picture->intra_dc_precision);

    if (picture->mpeg1) {
	if (picture->picture_coding_type != D_TYPE)
	    get_mpeg1_intra_block (picture, slice, DCTblock);
    } else if (picture->intra_vlc_format)
	get_intra_block_B15 (picture, slice, DCTblock);
    else
	get_intra_block_B14 (picture, slice, DCTblock);
    idct_block_copy (DCTblock, dest, stride);
    memset (DCTblock, 0, sizeof (DCTblock));
#undef bit_buf
#undef bits
}

static inline void slice_non_intra_DCT (picture_t * picture, slice_t * slice,
					uint8_t * dest, int stride)
{
    if (picture->mpeg1)
	get_mpeg1_non_intra_block (picture, slice, DCTblock);
    else
	get_non_intra_block (picture, slice, DCTblock);
    idct_block_add (DCTblock, dest, stride);
    memset (DCTblock, 0, sizeof (DCTblock));
}

static inline void motion_block (void (** table) (uint8_t *, uint8_t *,
						  int32_t, int32_t), 
				 int x_pred, int y_pred,
				 uint8_t * dest[3], int dest_offset,
				 uint8_t * src[3], int src_offset,
				 int stride, int height)
{
    int xy_half;
    uint8_t * src1;
    uint8_t * src2;

    xy_half = ((y_pred & 1) << 1) | (x_pred & 1);

    src1 = src[0] + src_offset + (x_pred >> 1) + (y_pred >> 1) * stride;

    table[xy_half] (dest[0] + dest_offset, src1, stride, height);

    x_pred /= 2;
    y_pred /= 2;

    xy_half = ((y_pred & 1) << 1) | (x_pred & 1);
    stride >>= 1;
    height >>= 1;
    src_offset >>= 1;
    dest_offset >>= 1;

    src1 = src[1] + src_offset + (x_pred >> 1) + (y_pred >> 1) * stride;
    src2 = src[2] + src_offset + (x_pred >> 1) + (y_pred >> 1) * stride;

    table[4+xy_half] (dest[1] + dest_offset, src1, stride, height);
    table[4+xy_half] (dest[2] + dest_offset, src2, stride, height);
}

#define bit_buf bitstream_buf
#define bits bitstream_bits

static void motion_frame (motion_t * motion, uint8_t * dest[3],
			  int offset, int width,
			  void (** table) (uint8_t *, uint8_t *, int, int))
{
    int motion_x, motion_y;

    NEEDBITS (bit_buf, bits);
    motion_x = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits);
    motion_y = motion->pmv[0][1] + get_motion_delta (motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;

    motion_block (table, motion_x, motion_y, dest, offset,
		  motion->ref_frame, offset, width, 16);
}

static void motion_field (motion_t * motion, uint8_t * dest[3],
			  int offset, int width,
			  void (** table) (uint8_t *, uint8_t *, int, int))
{
    int vertical_field_select;
    int motion_x, motion_y;

    NEEDBITS (bit_buf, bits);
    vertical_field_select = UBITS (bit_buf, 1);
    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits);
    motion_y = (motion->pmv[0][1] >> 1) + get_motion_delta (motion->f_code[1]);
    //motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[0][1] = motion_y << 1;

    motion_block (table, motion_x, motion_y, dest, offset,
		  motion->ref_frame, offset + vertical_field_select * width,
		  width * 2, 8);

    NEEDBITS (bit_buf, bits);
    vertical_field_select = UBITS (bit_buf, 1);
    DUMPBITS (bit_buf, bits, 1);

    motion_x = motion->pmv[1][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion_x;

    NEEDBITS (bit_buf, bits);
    motion_y = (motion->pmv[1][1] >> 1) + get_motion_delta (motion->f_code[1]);
    //motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion_y << 1;

    motion_block (table, motion_x, motion_y, dest, offset + width,
		  motion->ref_frame, offset + vertical_field_select * width,
		  width * 2, 8);
}

static int motion_dmv_top_field_first;
static void motion_dmv (motion_t * motion, uint8_t * dest[3],
			int offset, int width,
			void (** table) (uint8_t *, uint8_t *, int, int))
{
    int motion_x, motion_y;
    int dmv_x, dmv_y;
    int m;
    int other_x, other_y;

    NEEDBITS (bit_buf, bits);
    motion_x = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;

    NEEDBITS (bit_buf, bits);
    dmv_x = get_dmv ();

    NEEDBITS (bit_buf, bits);
    motion_y = (motion->pmv[0][1] >> 1) + get_motion_delta (motion->f_code[1]);
    //motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y << 1;

    NEEDBITS (bit_buf, bits);
    dmv_y = get_dmv ();

    motion_block (mc_functions.put, motion_x, motion_y, dest, offset,
		  motion->ref_frame, offset, width * 2, 8);

    m = motion_dmv_top_field_first ? 1 : 3;
    other_x = ((motion_x * m + (motion_x > 0)) >> 1) + dmv_x;
    other_y = ((motion_y * m + (motion_y > 0)) >> 1) + dmv_y - 1;
    motion_block (mc_functions.avg, other_x, other_y, dest, offset,
		  motion->ref_frame, offset + width, width * 2, 8);

    motion_block (mc_functions.put, motion_x, motion_y, dest, offset + width,
		  motion->ref_frame, offset + width, width * 2, 8);

    m = motion_dmv_top_field_first ? 3 : 1;
    other_x = ((motion_x * m + (motion_x > 0)) >> 1) + dmv_x;
    other_y = ((motion_y * m + (motion_y > 0)) >> 1) + dmv_y + 1;
    motion_block (mc_functions.avg, other_x, other_y, dest, offset + width,
		  motion->ref_frame, offset, width * 2, 8);
}

// like motion_frame, but reuse previous motion vectors
static void motion_reuse (motion_t * motion, uint8_t * dest[3],
			  int offset, int width,
			  void (** table) (uint8_t *, uint8_t *, int, int))
{
    motion_block (table, motion->pmv[0][0], motion->pmv[0][1], dest, offset,
		  motion->ref_frame, offset, width, 16);
}

// like motion_frame, but use null motion vectors
static void motion_zero (motion_t * motion, uint8_t * dest[3],
			 int offset, int width,
			 void (** table) (uint8_t *, uint8_t *, int, int))
{
    motion_block (table, 0, 0, dest, offset,
		  motion->ref_frame, offset, width, 16);
}

// like motion_frame, but parsing without actual motion compensation
static void motion_conceal (motion_t * motion)
{
    int tmp;

    NEEDBITS (bit_buf, bits);
    tmp = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    tmp = bound_motion_vector (tmp, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = tmp;

    NEEDBITS (bit_buf, bits);
    tmp = motion->pmv[0][1] + get_motion_delta (motion->f_code[1]);
    tmp = bound_motion_vector (tmp, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = tmp;

    DUMPBITS (bit_buf, bits, 1); // remove marker_bit
}

#undef bit_buf
#undef bits

#define MOTION(routine,direction,slice,dest,offset,stride)	\
do {								\
    if ((direction) & MACROBLOCK_MOTION_FORWARD)		\
	routine (& ((slice).f_motion), dest, offset, stride,	\
		 mc_functions.put);				\
    if ((direction) & MACROBLOCK_MOTION_BACKWARD)		\
	routine (& ((slice).b_motion), dest, offset, stride,	\
		 ((direction) & MACROBLOCK_MOTION_FORWARD ?	\
		  mc_functions.avg : mc_functions.put));	\
} while (0)

#define CHECK_DISPLAY					\
do {							\
    if (offset == width) {				\
	slice.f_motion.ref_frame[0] += 16 * offset;	\
	slice.f_motion.ref_frame[1] += 4 * offset;	\
	slice.f_motion.ref_frame[2] += 4 * offset;	\
	slice.b_motion.ref_frame[0] += 16 * offset;	\
	slice.b_motion.ref_frame[1] += 4 * offset;	\
	slice.b_motion.ref_frame[2] += 4 * offset;	\
	dest[0] += 16 * offset;				\
	dest[1] += 4 * offset;				\
	dest[2] += 4 * offset;				\
	offset = 0;					\
    }							\
} while (0)

int slice_process (picture_t * picture, uint8_t code, uint8_t * buffer)
{
#define bit_buf bitstream_buf
#define bits bitstream_bits
    int mba; 
    int macroblock_modes;
    int width;
    uint8_t * dest[3];
    int offset;

    width = picture->coded_picture_width;
    mba = (code - 1) * (picture->coded_picture_width >> 4);
    offset = (code - 1) * width * 4;

    slice.f_motion.ref_frame[0] =
	picture->forward_reference_frame[0] + offset * 4;
    slice.f_motion.ref_frame[1] = picture->forward_reference_frame[1] + offset;
    slice.f_motion.ref_frame[2] = picture->forward_reference_frame[2] + offset;
    slice.f_motion.f_code[0] = picture->f_code[0][0];
    slice.f_motion.f_code[1] = picture->f_code[0][1];
    slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
    slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;
    slice.b_motion.ref_frame[0] =
	picture->backward_reference_frame[0] + offset * 4;
    slice.b_motion.ref_frame[1] =
	picture->backward_reference_frame[1] + offset;
    slice.b_motion.ref_frame[2] =
	picture->backward_reference_frame[2] + offset;
    slice.b_motion.f_code[0] = picture->f_code[1][0];
    slice.b_motion.f_code[1] = picture->f_code[1][1];
    slice.b_motion.pmv[0][0] = slice.b_motion.pmv[0][1] = 0;
    slice.b_motion.pmv[1][0] = slice.b_motion.pmv[1][1] = 0;

    if ((! HACK_MODE) && (picture->picture_coding_type == B_TYPE))
	offset = 0;

    dest[0] = picture->current_frame[0] + offset * 4;
    dest[1] = picture->current_frame[1] + offset;
    dest[2] = picture->current_frame[2] + offset;

    //reset intra dc predictor
    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]= 
	1<< (picture->intra_dc_precision + 7) ;

    bitstream_init (buffer);

    slice.quantizer_scale = get_quantizer_scale (picture->q_scale_type);

    //Ignore intra_slice and all the extra data
    while (bit_buf & 0x80000000) {
	DUMPBITS (bit_buf, bits, 9);
	NEEDBITS (bit_buf, bits);
    }
    DUMPBITS (bit_buf, bits, 1);

    offset = get_macroblock_address_increment ();
    mba += offset;
    offset <<= 4;

    while (1) {
	NEEDBITS (bit_buf, bits);

	macroblock_modes =
	    get_macroblock_modes (picture->picture_coding_type,
				  picture->frame_pred_frame_dct);

	// maybe integrate MACROBLOCK_QUANT test into get_macroblock_modes ?
	if (macroblock_modes & MACROBLOCK_QUANT)
	    slice.quantizer_scale =
		get_quantizer_scale (picture->q_scale_type);

	if (macroblock_modes & MACROBLOCK_INTRA) {

	    int DCT_offset, DCT_stride;

	    if (picture->concealment_motion_vectors)
		motion_conceal (&slice.f_motion);
	    else {
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;
		slice.b_motion.pmv[0][0] = slice.b_motion.pmv[0][1] = 0;
		slice.b_motion.pmv[1][0] = slice.b_motion.pmv[1][1] = 0;
	    }

	    if (macroblock_modes & DCT_TYPE_INTERLACED) {
		DCT_offset = width;
		DCT_stride = width * 2;
	    } else {
		DCT_offset = width * 8;
		DCT_stride = width;
	    }

	    // Decode lum blocks
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + 8, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + DCT_offset, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + DCT_offset + 8, DCT_stride);

	    // Decode chroma blocks
	    slice_intra_DCT (picture, &slice, 1,
			     dest[1] + (offset>>1), width>>1);
	    slice_intra_DCT (picture, &slice, 2,
			     dest[2] + (offset>>1), width>>1);

	    if (picture->picture_coding_type == D_TYPE) {
		NEEDBITS (bit_buf, bits);
		DUMPBITS (bit_buf, bits, 1);
	    }
	} else {

	    switch (macroblock_modes & MOTION_TYPE_MASK) {
	    case MC_FRAME:
		MOTION (motion_frame, macroblock_modes, slice, dest,
			offset, width);
		break;

	    case MC_FIELD:
		MOTION (motion_field, macroblock_modes, slice, dest,
			offset, width);
		break;

	    case MC_DMV:
		motion_dmv_top_field_first = picture->top_field_first;
		MOTION (motion_dmv, MACROBLOCK_MOTION_FORWARD, slice, dest,
			offset, width);
		break;

	    case 0:
		// non-intra mb without forward mv in a P picture
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;

		MOTION (motion_zero, MACROBLOCK_MOTION_FORWARD,
			slice, dest, offset, width);
		break;
	    }

	    //6.3.17.4 Coded block pattern
	    if (macroblock_modes & MACROBLOCK_PATTERN) {
		int coded_block_pattern;
		int DCT_offset, DCT_stride;

		if (macroblock_modes & DCT_TYPE_INTERLACED) {
		    DCT_offset = width;
		    DCT_stride = width * 2;
		} else {
		    DCT_offset = width * 8;
		    DCT_stride = width;
		}

		coded_block_pattern = get_coded_block_pattern ();

		// Decode lum blocks

		if (coded_block_pattern & 0x20)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset, DCT_stride);
		if (coded_block_pattern & 0x10)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + 8, DCT_stride);
		if (coded_block_pattern & 0x08)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + DCT_offset,
					 DCT_stride);
		if (coded_block_pattern & 0x04)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + DCT_offset + 8,
					 DCT_stride);

		// Decode chroma blocks

		if (coded_block_pattern & 0x2)
		    slice_non_intra_DCT (picture, &slice,
					 dest[1] + (offset>>1), width >> 1);
		if (coded_block_pattern & 0x1)
		    slice_non_intra_DCT (picture, &slice,
					 dest[2] + (offset>>1), width >> 1);
	    }

	    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]=
		1 << (picture->intra_dc_precision + 7);
	}

	mba++;
	offset += 16;
	CHECK_DISPLAY;

	NEEDBITS (bit_buf, bits);

	if (bit_buf & 0x80000000) {
	    DUMPBITS (bit_buf, bits, 1);
	} else {
	    int mba_inc;

	    mba_inc = get_macroblock_address_increment ();
	    if (!mba_inc)
		break;

	    //reset intra dc predictor on skipped block
	    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]=
		1<< (picture->intra_dc_precision + 7);

	    //handling of skipped mb's differs between P_TYPE and B_TYPE
	    //pictures
	    if (picture->picture_coding_type == P_TYPE) {
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;

		do {
		    MOTION (motion_zero, MACROBLOCK_MOTION_FORWARD,
			    slice, dest, offset, width);

		    mba++;
		    offset += 16;
		    CHECK_DISPLAY;
		} while (--mba_inc);
	    } else {
		do {
		    MOTION (motion_reuse, macroblock_modes,
			    slice, dest, offset, width);

		    mba++;
		    offset += 16;
		    CHECK_DISPLAY;
		} while (--mba_inc);
	    }
	}
    }

    return (mba > picture->last_mba);
#undef bit_buf
#undef bits
}
