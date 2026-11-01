/* ------------------------------------------------------------------------
 *   JPEG in and out (libturbojpeg): the full image and thumbnail the
 *   Channel API wants for send_image, and decoded thumbnails for the
 *   history view.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_IMAGE_H
#define SYN_CAST_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define IMAGE_FULL_MAX 1280
#define IMAGE_THUMB_MAX 160

/* Reads a JPEG (or BMP/PPM), returns a re-encoded full image (longest side
 * at most IMAGE_FULL_MAX) and thumbnail. All buffers are malloc'd. */
int image_prepare(const char *path, uint8_t **full, size_t *full_n, uint8_t **thumb,
	size_t *thumb_n, int *w, int *h, char *err, size_t errn);

/* Decodes a JPEG file to XRGB8888 scaled to fit max_w x max_h. */
uint32_t *image_load_argb(const char *path, int max_w, int max_h, int *w, int *h);

#endif
