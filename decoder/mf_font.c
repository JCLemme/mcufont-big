#include "mf_font.h"
#include "mf_bwfont.h"
#include "mf_rlefont.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* This will be made into a list of included fonts using macro magic. */
#define MF_INCLUDED_FONTS 0

/* Included fonts begin here */
#include MF_FONT_FILE_NAME
/* Include fonts end here */

uint8_t mf_render_character(const struct mf_font_s *font,
                            int16_t x0, int16_t y0,
                            mf_char character,
                            mf_pixel_callback_t callback,
                            void *state)
{
    uint8_t width;
    width = font->render_character(font, x0, y0, character, callback, state);

    if (!width)
    {
        width = font->render_character(font, x0, y0, font->fallback_character,
                                       callback, state);
    }

    return width;
}

uint8_t mf_character_width(const struct mf_font_s *font,
                           mf_char character)
{
    uint8_t width;
    width = font->character_width(font, character);

    if (!width)
    {
        width = font->character_width(font, font->fallback_character);
    }

    return width;
}

struct whitespace_state
{
    uint8_t min_x, min_y;
    uint8_t max_x, max_y;
};

static void whitespace_callback(int16_t x, int16_t y, uint8_t count,
                                uint8_t alpha, void *state)
{
    struct whitespace_state *s = state;
    if (alpha > 7)
    {
        if (s->min_x > x) s->min_x = x;
        if (s->min_y > y) s->min_y = y;
        x += count - 1;
        if (s->max_x < x) s->max_x = x;
        if (s->max_y < y) s->max_y = y;
    }
}

MF_EXTERN void mf_character_whitespace(const struct mf_font_s *font,
                                       mf_char character,
                                       uint8_t *left, uint8_t *top,
                                       uint8_t *right, uint8_t *bottom)
{
    struct whitespace_state state = {255, 255, 0, 0};
    mf_render_character(font, 0, 0, character, whitespace_callback, &state);

    if (state.min_x == 255 && state.min_y == 255)
    {
        /* Character is whitespace */
        if (left) *left = font->width;
        if (top) *top = font->height;
        if (right) *right = 0;
        if (bottom) *bottom = 0;
    }
    else
    {
        if (left) *left = state.min_x;
        if (top) *top = state.min_y;
        if (right) *right = font->width - state.max_x - 1;
        if (bottom) *bottom = font->height - state.max_y - 1;
    }
}

/* Avoids a dependency on libc */
static bool strequals(const char *a, const char *b)
{
    while (*a)
    {
        if (*a++ != *b++)
            return false;
    }
    return (!*b);
}

const struct mf_font_s *mf_find_font(const char *name)
{
    const struct mf_font_list_s *f;
    f = MF_INCLUDED_FONTS;

    while (f)
    {
        if (strequals(f->font->full_name, name) ||
            strequals(f->font->short_name, name))
        {
            return f->font;
        }

        f = f->next;
    }

    return 0;
}

const struct mf_font_list_s *mf_get_font_list(void)
{
    return MF_INCLUDED_FONTS;
}

// Note to self: the alpha value is actually 0-16, so you can draw an rle font by thresholding 128

struct mf_font_s* mf_make_font(uint8_t* bulk, uint32_t len)
{
    struct mf_font_s* built;
    uint32_t run = 4;

    // Determine type and version.
    if(strequals("ftbw", (char*)bulk))
    //if(bulk[0] == 'f' && bulk[1] == 't' && bulk[2] == 'b' && bulk[3] == 'w')
    {
        // Black and white fonts get a flag.
        built = calloc(1, sizeof(struct mf_bwfont_s));
        built->flags = MF_FONT_FLAG_BW;
    }
    else if(strequals("ftrl", (char*)bulk))
    //else if(bulk[0] == 'f' && bulk[1] == 't' && bulk[2] == 'r' && bulk[3] == 'l')
    {
        // Normal run-length encoded font.
        built = calloc(1, sizeof(struct mf_rlefont_s));
    }
    else
    {
        // No idea what this is.
        return NULL;
    }

    uint8_t typecase_version = bulk[run++];
    uint8_t font_version = bulk[run++];

    if(typecase_version != MF_TYPECASE_VERSION_SUPPORTED)
    {
        free(built);
        return NULL;
    }

    // Load the common font fields...
    built->width = bulk[run++];
    built->height = bulk[run++];
    built->min_x_advance = bulk[run++];
    built->max_x_advance = bulk[run++];
    built->baseline_x = bulk[run++];
    built->baseline_y = bulk[run++];
    built->line_height = bulk[run++];
    built->flags = bulk[run++];

    // Little endian u16.
    built->fallback_character = *((uint16_t*)(bulk+run)); run += 2;

    // Pascal strings too.
    uint8_t slen = bulk[run++];
    built->full_name = calloc(slen+2, sizeof(char));
    memcpy(built->full_name, bulk+run, slen);
    run += slen;

    slen = bulk[run++];
    built->short_name = calloc(slen+2, sizeof(char));
    memcpy(built->short_name, bulk+run, slen);
    run += slen;

    // Time for details.
    if(built->flags & MF_FONT_FLAG_BW)
    {
        // Black and white fonts don't have much going on.
        struct mf_bwfont_s* builtbw = (struct mf_bwfont_s*)built;
        builtbw->version = font_version;
        builtbw->char_range_count = bulk[run++];

        // Sidebar: set the appropriate handler functions too.
        built->character_width =  &mf_bwfont_character_width;
        built->render_character = &mf_bwfont_render_character;

        // We'll need to allocate the ranges separately.
        builtbw->char_ranges = calloc(builtbw->char_range_count, sizeof(struct mf_bwfont_char_range_s));

        // Ready aim fire
        for(int r = 0; r < builtbw->char_range_count; r++)
        {
            struct mf_bwfont_char_range_s* range = &(builtbw->char_ranges[r]);
            
            // Little endian u16s.
            range->first_char = *((uint16_t*)(bulk+run)); run += 2;
            range->char_count = *((uint16_t*)(bulk+run)); run += 2;

            range->offset_x = bulk[run++];
            range->offset_y = bulk[run++];
            range->height_bytes = bulk[run++];
            range->height_pixels = bulk[run++];
            range->width = bulk[run++];

            // These next fields are stored as offsets into the byte array, so make the math work.
            // Also pay attention to the nulls - fixed fonts don't need width information.
            uint32_t offs = *((uint32_t*)(bulk+run)); run += 4;
            range->glyph_widths = (range->width) ? NULL : (uint8_t*)(bulk+offs);

            offs = *((uint32_t*)(bulk+run)); run += 4;
            range->glyph_offsets = (range->width) ? NULL : (uint16_t*)(bulk+offs);

            offs = *((uint32_t*)(bulk+run)); run += 4;
            range->glyph_data = (uint8_t*)(bulk+offs);
        }
    }
    else
    {
        //("panic! loading rle fonts is not implemented yet\n");
        return NULL;
    }

    // Good work everyone.
    return built;
}

void mf_destroy_font(struct mf_font_s* target)
{
    if(target->flags & MF_FONT_FLAG_BW)
    {
        free(((struct mf_bwfont_s*)(target))->char_ranges);
    }
    else
    {
        // Not done with this yet.
    }

    free(target->short_name);
    free(target->full_name);
    free(target);
}
