/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2013 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RPNG_H__
#define RPNG_H__

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rpng_load_image_argb(const char *path, uint32_t **data, unsigned *width, unsigned *height);

/* Decodes every frame of an APNG at 'path' into an array of ARGB
 * buffers of identical dimensions.  A still PNG loads as a single
 * frame, so callers need not distinguish the two.  On success returns
 * the frame count and stores a malloc'd array of malloc'd frames in
 * *frames; the caller frees each frame and then the array.  Returns 0
 * on failure with *frames set to NULL. */
unsigned rpng_load_apng_argb(const char *path, uint32_t ***frames,
      unsigned *width, unsigned *height);

#ifdef __cplusplus
}
#endif

#endif

