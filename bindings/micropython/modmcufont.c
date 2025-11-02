/*
 * MicroPython module for mcufont decoder library
 * Provides Python bindings for compressed bitmap font rendering
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/objtype.h"
#include "py/binary.h"
#include "py/parsenum.h"
#include "mcufont/decoder/mcufont.h"

// Python object for mf_font_s
typedef struct _mp_obj_font_t {
    mp_obj_base_t base;
    const struct mf_font_s *font;
    mp_obj_t font_data;  // bytearray containing font data (mp_const_none if static font)
} mp_obj_font_t;

// Python object for mf_rlefont_s
typedef struct _mp_obj_rlefont_t {
    mp_obj_base_t base;
    const struct mf_rlefont_s *rlefont;
} mp_obj_rlefont_t;

// Python object for mf_bwfont_s
typedef struct _mp_obj_bwfont_t {
    mp_obj_base_t base;
    const struct mf_bwfont_s *bwfont;
} mp_obj_bwfont_t;

// Python object for mf_scaledfont_s
typedef struct _mp_obj_scaledfont_t {
    mp_obj_base_t base;
    struct mf_scaledfont_s scaledfont;
} mp_obj_scaledfont_t;

// Global pixel callback state for Python callback
typedef struct {
    mp_obj_t callback;
    mp_obj_t state;
} pixel_callback_state_t;

// C callback that calls Python pixel callback
static void pixel_callback_wrapper(int16_t x, int16_t y, uint8_t count, uint8_t alpha, void *state) {
    pixel_callback_state_t *cb_state = (pixel_callback_state_t *)state;
    if (cb_state->callback != mp_const_none) {
        mp_obj_t args[5] = {
            //cb_state->callback,
            mp_obj_new_int(x),
            mp_obj_new_int(y), 
            mp_obj_new_int(count),
            mp_obj_new_int(alpha),
            cb_state->state
        };
        if (cb_state->state != mp_const_none) {
            mp_call_function_n_kw(cb_state->callback, 5, 0, args);
        } else {
            mp_call_function_n_kw(cb_state->callback, 4, 0, args);
        }
    }
}

// Global character callback state for Python callback
typedef struct {
    mp_obj_t callback;
    mp_obj_t state;
} character_callback_state_t;

// C callback that calls Python character callback
static uint8_t character_callback_wrapper(int16_t x0, int16_t y0, mf_char character, void *state) {
    character_callback_state_t *cb_state = (character_callback_state_t *)state;
    if (cb_state->callback != mp_const_none) {
        mp_obj_t args[4] = {
            mp_obj_new_int(x0),
            mp_obj_new_int(y0),
            mp_obj_new_int(character),
            cb_state->state
        };
        mp_obj_t result = mp_call_function_n_kw(cb_state->callback, 4, 0, args);
        return mp_obj_get_int(result);
    }
    return 0;
}

// Line callback wrapper for word wrap
typedef struct {
    mp_obj_t callback;
    mp_obj_t state;
} line_callback_state_t;

static bool line_callback_wrapper(mf_str line, uint16_t count, void *state) {
    line_callback_state_t *cb_state = (line_callback_state_t *)state;
    if (cb_state->callback != mp_const_none) {
        mp_obj_t line_str = mp_obj_new_str((const char*)line, count);
        mp_obj_t args[3] = {
            line_str,
            mp_obj_new_int(count),
            cb_state->state
        };
        mp_obj_t result = mp_call_function_n_kw(cb_state->callback, 3, 0, args);
        return mp_obj_is_true(result);
    }
    return false;
}

//
// Font object implementation
//
static void font_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<Font '%s' %dx%d>", 
              self->font->short_name, 
              self->font->width, 
              self->font->height);
}

static mp_obj_t font_del(mp_obj_t self_in) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(self_in);
    // Only destroy fonts that were created from byte data
    if (self->font_data != mp_const_none) {
        mf_destroy_font((struct mf_font_s*)self->font);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(font_del_obj, font_del);

static mp_obj_t font_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    
    const char *name = mp_obj_str_get_str(args[0]);
    const struct mf_font_s *font = mf_find_font(name);
    
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("Font not found"));
    }
    
    mp_obj_font_t *o = mp_obj_malloc(mp_obj_font_t, type);
    o->font = font;
    o->font_data = mp_const_none;  // Static font, no bytearray
    return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t font_render_character(size_t n_args, const mp_obj_t *args) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(args[0]);
    int16_t x0 = mp_obj_get_int(args[1]);
    int16_t y0 = mp_obj_get_int(args[2]);
    mf_char character = mp_obj_get_int(args[3]);
    mp_obj_t callback = args[4];
    mp_obj_t state = (n_args > 5) ? args[5] : mp_const_none;
    
    pixel_callback_state_t cb_state = { callback, state };
    uint8_t width = mf_render_character(self->font, x0, y0, character, 
                                        pixel_callback_wrapper, &cb_state);
    return mp_obj_new_int(width);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(font_render_character_obj, 5, 6, font_render_character);

static mp_obj_t font_character_width(mp_obj_t self_in, mp_obj_t character_in) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(self_in);
    mf_char character = mp_obj_get_int(character_in);
    uint8_t width = mf_character_width(self->font, character);
    return mp_obj_new_int(width);
}
static MP_DEFINE_CONST_FUN_OBJ_2(font_character_width_obj, font_character_width);

static mp_obj_t font_character_whitespace(mp_obj_t self_in, mp_obj_t character_in) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(self_in);
    mf_char character = mp_obj_get_int(character_in);
    uint8_t left, top, right, bottom;
    mf_character_whitespace(self->font, character, &left, &top, &right, &bottom);
    
    mp_obj_t tuple[4] = {
        mp_obj_new_int(left),
        mp_obj_new_int(top),
        mp_obj_new_int(right),
        mp_obj_new_int(bottom)
    };
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_2(font_character_whitespace_obj, font_character_whitespace);

static void font_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_font_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute
        switch (attr) {
            case MP_QSTR_full_name:
                dest[0] = mp_obj_new_str(self->font->full_name, strlen(self->font->full_name));
                return;
            case MP_QSTR_short_name:
                dest[0] = mp_obj_new_str(self->font->short_name, strlen(self->font->short_name));
                return;
            case MP_QSTR_width:
                dest[0] = mp_obj_new_int(self->font->width);
                return;
            case MP_QSTR_height:
                dest[0] = mp_obj_new_int(self->font->height);
                return;
            case MP_QSTR_min_x_advance:
                dest[0] = mp_obj_new_int(self->font->min_x_advance);
                return;
            case MP_QSTR_max_x_advance:
                dest[0] = mp_obj_new_int(self->font->max_x_advance);
                return;
            case MP_QSTR_baseline_x:
                dest[0] = mp_obj_new_int(self->font->baseline_x);
                return;
            case MP_QSTR_baseline_y:
                dest[0] = mp_obj_new_int(self->font->baseline_y);
                return;
            case MP_QSTR_line_height:
                dest[0] = mp_obj_new_int(self->font->line_height);
                return;
            case MP_QSTR_flags:
                dest[0] = mp_obj_new_int(self->font->flags);
                return;
            case MP_QSTR_fallback_character:
                dest[0] = mp_obj_new_int(self->font->fallback_character);
                return;
            case MP_QSTR_font_data:
                dest[0] = self->font_data;
                return;
            default:
                dest[1] = MP_OBJ_SENTINEL;
                return;
        }
    }
}


static const mp_rom_map_elem_t font_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_render_character), MP_ROM_PTR(&font_render_character_obj) },
    { MP_ROM_QSTR(MP_QSTR_character_width), MP_ROM_PTR(&font_character_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_character_whitespace), MP_ROM_PTR(&font_character_whitespace_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&font_del_obj) },
};
static MP_DEFINE_CONST_DICT(font_locals_dict, font_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_font,
    MP_QSTR_Font,
    MP_TYPE_FLAG_NONE,
    make_new, font_make_new,
    print, font_print,
    attr, font_attr,
    locals_dict, &font_locals_dict
);



//
// ScaledFont object implementation
//
static void scaledfont_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_obj_scaledfont_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<ScaledFont %dx%d scale=%dx%d>", 
              self->scaledfont.font.width, 
              self->scaledfont.font.height,
              self->scaledfont.x_scale,
              self->scaledfont.y_scale);
}

static mp_obj_t scaledfont_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 3, 3, false);
    
    if (!mp_obj_is_type(args[0], &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *basefont_obj = MP_OBJ_TO_PTR(args[0]);
    uint8_t x_scale = mp_obj_get_int(args[1]);
    uint8_t y_scale = mp_obj_get_int(args[2]);
    
    mp_obj_scaledfont_t *o = mp_obj_malloc(mp_obj_scaledfont_t, type);
    mf_scale_font(&o->scaledfont, basefont_obj->font, x_scale, y_scale);
    return MP_OBJ_FROM_PTR(o);
}

static void scaledfont_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_obj_scaledfont_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute - delegate to font attributes
        switch (attr) {
            case MP_QSTR_x_scale:
                dest[0] = mp_obj_new_int(self->scaledfont.x_scale);
                return;
            case MP_QSTR_y_scale:
                dest[0] = mp_obj_new_int(self->scaledfont.y_scale);
                return;
            default: {
                // Create a temporary font object to get other attributes
                mp_obj_font_t temp_font = { {&mp_type_font}, &self->scaledfont.font, NULL};
                font_attr(MP_OBJ_FROM_PTR(&temp_font), attr, dest);
                return;
            }
        }
    }
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_scaledfont,
    MP_QSTR_ScaledFont,
    MP_TYPE_FLAG_NONE,
    make_new, scaledfont_make_new,
    print, scaledfont_print,
    attr, scaledfont_attr
);



//
// Module-level functions
//

// Encoding functions
static mp_obj_t mcufont_getchar(mp_obj_t str_in) {
    const char *str = mp_obj_str_get_str(str_in);
    mf_str mf_string = str;
    mf_char character = mf_getchar(&mf_string);
    return mp_obj_new_int(character);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mcufont_getchar_obj, mcufont_getchar);

static mp_obj_t mcufont_rewind(mp_obj_t str_in) {
    // Note: This function modifies the string pointer position, but since Python strings
    // are immutable, we cannot actually modify the original string pointer.
    // This function is provided for completeness but has limited utility in Python context.
    mp_raise_NotImplementedError(MP_ERROR_TEXT("mf_rewind not supported with Python strings"));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mcufont_rewind_obj, mcufont_rewind);

// Text alignment and justification
static mp_obj_t mcufont_get_string_width(size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(args[0], &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *font_obj = MP_OBJ_TO_PTR(args[0]);
    const char *text = mp_obj_str_get_str(args[1]);
    uint16_t count = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    bool kern = (n_args > 3) ? mp_obj_is_true(args[3]) : false;
    
    int16_t width = mf_get_string_width(font_obj->font, text, count, kern);
    return mp_obj_new_int(width);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mcufont_get_string_width_obj, 2, 4, mcufont_get_string_width);

static mp_obj_t mcufont_render_aligned(size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(args[0], &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *font_obj = MP_OBJ_TO_PTR(args[0]);
    int16_t x0 = mp_obj_get_int(args[1]);
    int16_t y0 = mp_obj_get_int(args[2]);
    enum mf_align_t align = mp_obj_get_int(args[3]);
    const char *text = mp_obj_str_get_str(args[4]);
    uint16_t count = mp_obj_get_int(args[5]);
    mp_obj_t callback = args[6];
    mp_obj_t state = (n_args > 7) ? args[7] : mp_const_none;
    
    character_callback_state_t cb_state = { callback, state };
    mf_render_aligned(font_obj->font, x0, y0, align, text, count, 
                      character_callback_wrapper, &cb_state);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mcufont_render_aligned_obj, 7, 8, mcufont_render_aligned);

static mp_obj_t mcufont_render_justified(size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(args[0], &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *font_obj = MP_OBJ_TO_PTR(args[0]);
    int16_t x0 = mp_obj_get_int(args[1]);
    int16_t y0 = mp_obj_get_int(args[2]);
    int16_t width = mp_obj_get_int(args[3]);
    const char *text = mp_obj_str_get_str(args[4]);
    uint16_t count = mp_obj_get_int(args[5]);
    mp_obj_t callback = args[6];
    mp_obj_t state = (n_args > 7) ? args[7] : mp_const_none;
    
    character_callback_state_t cb_state = { callback, state };
    mf_render_justified(font_obj->font, x0, y0, width, text, count, 
                        character_callback_wrapper, &cb_state);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mcufont_render_justified_obj, 7, 8, mcufont_render_justified);

// Kerning
#if MF_USE_KERNING
static mp_obj_t mcufont_compute_kerning(mp_obj_t font_in, mp_obj_t c1_in, mp_obj_t c2_in) {
    if (!mp_obj_is_type(font_in, &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *font_obj = MP_OBJ_TO_PTR(font_in);
    mf_char c1 = mp_obj_get_int(c1_in);
    mf_char c2 = mp_obj_get_int(c2_in);
    
    int8_t kerning = mf_compute_kerning(font_obj->font, c1, c2);
    return mp_obj_new_int(kerning);
}
static MP_DEFINE_CONST_FUN_OBJ_3(mcufont_compute_kerning_obj, mcufont_compute_kerning);
#endif

// Word wrapping
static mp_obj_t mcufont_wordwrap(size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(args[0], &mp_type_font)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected Font object"));
    }
    
    mp_obj_font_t *font_obj = MP_OBJ_TO_PTR(args[0]);
    int16_t width = mp_obj_get_int(args[1]);
    const char *text = mp_obj_str_get_str(args[2]);
    mp_obj_t callback = args[3];
    mp_obj_t state = (n_args > 4) ? args[4] : mp_const_none;
    
    line_callback_state_t cb_state = { callback, state };
    mf_wordwrap(font_obj->font, width, text, line_callback_wrapper, &cb_state);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mcufont_wordwrap_obj, 4, 5, mcufont_wordwrap);

// Font finding
static mp_obj_t mcufont_find_font(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    const struct mf_font_s *font = mf_find_font(name);
    
    if (!font) {
        return mp_const_none;
    }
    
    mp_obj_font_t *o = mp_obj_malloc(mp_obj_font_t, &mp_type_font);
    o->font = font;
    o->font_data = mp_const_none;  // Static font, no bytearray
    return MP_OBJ_FROM_PTR(o);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mcufont_find_font_obj, mcufont_find_font);

static mp_obj_t mcufont_get_font_list(void) {
    const struct mf_font_list_s *font_list = mf_get_font_list();
    mp_obj_t list = mp_obj_new_list(0, NULL);
    
    while (font_list) {
        mp_obj_font_t *font_obj = mp_obj_malloc(mp_obj_font_t, &mp_type_font);
        font_obj->font = font_list->font;
        font_obj->font_data = mp_const_none;  // Static font, no bytearray
        mp_obj_list_append(list, MP_OBJ_FROM_PTR(font_obj));
        font_list = font_list->next;
    }
    
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mcufont_get_font_list_obj, mcufont_get_font_list);

// Font creation from bytes
static mp_obj_t mcufont_font_from_bytes(mp_obj_t bytes_obj) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(bytes_obj, &bufinfo, MP_BUFFER_READ);
    
    // Create a font from the byte data
    struct mf_font_s *font = mf_make_font((uint8_t*)bufinfo.buf, bufinfo.len);
    if (!font) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid font data"));
    }
    
    mp_obj_font_t *o = m_new_obj(mp_obj_font_t);
    o->base.type = &mp_type_font;
    o->font = font;
    o->font_data = bytes_obj;  // Keep reference to prevent GC
    return MP_OBJ_FROM_PTR(o);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mcufont_font_from_bytes_obj, mcufont_font_from_bytes);

// Module globals
static const mp_rom_map_elem_t mcufont_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mcufont) },
    
    // Classes
    { MP_ROM_QSTR(MP_QSTR_Font), MP_ROM_PTR(&mp_type_font) },
    { MP_ROM_QSTR(MP_QSTR_ScaledFont), MP_ROM_PTR(&mp_type_scaledfont) },
    
    // Functions
    { MP_ROM_QSTR(MP_QSTR_getchar), MP_ROM_PTR(&mcufont_getchar_obj) },
    { MP_ROM_QSTR(MP_QSTR_rewind), MP_ROM_PTR(&mcufont_rewind_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_string_width), MP_ROM_PTR(&mcufont_get_string_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_render_aligned), MP_ROM_PTR(&mcufont_render_aligned_obj) },
    { MP_ROM_QSTR(MP_QSTR_render_justified), MP_ROM_PTR(&mcufont_render_justified_obj) },
    { MP_ROM_QSTR(MP_QSTR_wordwrap), MP_ROM_PTR(&mcufont_wordwrap_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_font), MP_ROM_PTR(&mcufont_find_font_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_font_list), MP_ROM_PTR(&mcufont_get_font_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_from_bytes), MP_ROM_PTR(&mcufont_font_from_bytes_obj) },
    
#if MF_USE_KERNING
    { MP_ROM_QSTR(MP_QSTR_compute_kerning), MP_ROM_PTR(&mcufont_compute_kerning_obj) },
#endif
    
    // Constants
    { MP_ROM_QSTR(MP_QSTR_ALIGN_LEFT), MP_ROM_INT(MF_ALIGN_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_CENTER), MP_ROM_INT(MF_ALIGN_CENTER) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_RIGHT), MP_ROM_INT(MF_ALIGN_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_FONT_FLAG_MONOSPACE), MP_ROM_INT(MF_FONT_FLAG_MONOSPACE) },
    { MP_ROM_QSTR(MP_QSTR_FONT_FLAG_BW), MP_ROM_INT(MF_FONT_FLAG_BW) },
};

static MP_DEFINE_CONST_DICT(mcufont_module_globals, mcufont_module_globals_table);

const mp_obj_module_t mp_module_mcufont = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mcufont_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mcufont, mp_module_mcufont);
