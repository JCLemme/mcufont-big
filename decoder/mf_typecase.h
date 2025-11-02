/* A collection of loadable fonts.
 */

#ifndef _MF_TYPECASE_H_
#define _MF_TYPECASE_H_

#include "mf_font.h"
#include "mf_bwfont.h"
#include "mf_rlefont.h"

#define MF_TYPECASE_MAGIC "case"
#define MF_BWFONT_MAGIC "ftbw"
#define MF_RLEFONT_MAGIC "ftrl"

struct mf_face_s
{
    const char* name;
    uint8_t size;
    uint32_t offset;    // Location in file
    uint32_t next;
};

struct mf_typecase_s
{
    uint32_t magic;
    uint8_t version;
    uint16_t n_faces;
    struct mf_face_s faces[];
};

#endif
