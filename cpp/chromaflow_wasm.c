#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <png.h>
#include "jabcode.h"

/*
 * chromaflow_wasm.c — thin WASM-facing wrappers around libjabcode.
 *
 * Exported functions (called from JS via cwrap):
 *
 *   uint8_t* cf_encode(const uint8_t* data,    int data_len,
 *                      int color_number,        int module_size,
 *                      int symbol_width,        int symbol_height,
 *                      int ecc_level,
 *                      int* out_png_len)
 *
 *   uint8_t* cf_decode(const uint8_t* png_data, int png_len,
 *                      int* out_data_len)
 *
 *   void     cf_free(uint8_t* ptr)
 *
 * Both encode and decode return a malloc'd buffer that the caller must
 * release with cf_free() once it has copied the data into a JS typed array.
 * NULL is returned on failure; out_*_len is set to 0 in that case.
 *
 * Compile additions (append to the existing emmake line):
 *   -sEXPORTED_FUNCTIONS="['_cf_encode','_cf_decode','_cf_free','_malloc','_free']"
 *   -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap']"
 */

/* ── Encoder ──────────────────────────────────────────────────────────────── */

/*
 * Encode raw bytes into a JABCode PNG, entirely in memory.
 *
 * Parameters mirror the subset of jabwriter CLI options that ChromaFlow
 * actually uses.  Pass 0 for symbol_width / symbol_height to let the
 * library choose.  ecc_level 0 uses the library default (3).
 *
 * Returns a malloc'd PNG buffer.  *out_png_len is set to its byte length.
 * Returns NULL on any failure.
 */
uint8_t* cf_encode(const uint8_t* data,
                   int            data_len,
                   int            color_number,
                   int            module_size,
                   int            symbol_width,
                   int            symbol_height,
                   int            ecc_level,
                   int*           out_png_len)
{
    *out_png_len = 0;

    /* ── 1. Wrap input bytes in jab_data ── */
    jab_data* jdata = (jab_data*)malloc(sizeof(jab_data) + data_len);
    if (!jdata) return NULL;
    jdata->length = data_len;
    memcpy(jdata->data, data, data_len);

    /* ── 2. Create encoder (single-symbol, options matching writer defaults) ── */
    jab_encode* enc = createEncode(color_number > 0 ? color_number : 8, 1);
    if (!enc) { free(jdata); return NULL; }

    if (module_size  > 0) enc->module_size          = module_size;
    if (symbol_width > 0) enc->master_symbol_width  = symbol_width;
    if (symbol_height> 0) enc->master_symbol_height = symbol_height;
    if (ecc_level    > 0) enc->symbol_ecc_levels[0] = (jab_byte)ecc_level;

    /* ── 3. Generate JABCode bitmap ── */
    if (generateJABCode(enc, jdata) != 0) {
        destroyEncode(enc);
        free(jdata);
        return NULL;
    }
    free(jdata);

    jab_bitmap* bmp = enc->bitmap;

    /* ── 4. Encode bitmap → PNG in memory using libpng simplified API ──
     *
     * png_image_write_to_memory requires two passes:
     *   pass 1 — buffer=NULL         → libpng writes required size into memory_bytes
     *   pass 2 — buffer=malloc(size) → libpng writes PNG bytes
     */
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (bmp->channel_count == 4) {
        image.format = PNG_FORMAT_RGBA;
        image.flags  = PNG_FORMAT_FLAG_ALPHA | PNG_FORMAT_FLAG_COLOR;
    } else {
        image.format = PNG_FORMAT_GRAY;
    }
    image.width  = bmp->width;
    image.height = bmp->height;

    /* pass 1: size probe */
    png_alloc_size_t png_size = 0;
    if (!png_image_write_to_memory(&image, NULL, &png_size,
                                   0/*convert_to_8bit*/,
                                   bmp->pixel,
                                   0/*row_stride*/,
                                   NULL/*colormap*/)) {
        destroyEncode(enc);
        return NULL;
    }

    /* pass 2: actual write */
    uint8_t* png_buf = (uint8_t*)malloc(png_size);
    if (!png_buf) { destroyEncode(enc); return NULL; }

    if (!png_image_write_to_memory(&image, png_buf, &png_size,
                                   0, bmp->pixel, 0, NULL)) {
        free(png_buf);
        destroyEncode(enc);
        return NULL;
    }

    destroyEncode(enc);
    *out_png_len = (int)png_size;
    return png_buf;
}

/* ── Decoder ──────────────────────────────────────────────────────────────── */

/*
 * Decode a JABCode PNG (raw bytes) and return the embedded payload.
 *
 * Returns a malloc'd buffer containing the raw decoded bytes.
 * *out_data_len is set to its length.
 * Returns NULL if the image cannot be decoded.
 */
uint8_t* cf_decode(const uint8_t* png_data,
                   int            png_len,
                   int*           out_data_len)
{
    *out_data_len = 0;

    /* ── 1. PNG bytes → jab_bitmap using libpng simplified API ── */
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, png_data, (png_size_t)png_len)) {
        return NULL;
    }

    image.format = PNG_FORMAT_RGBA;   /* match readImage() behaviour */

    jab_bitmap* bmp = (jab_bitmap*)calloc(
        1, sizeof(jab_bitmap) + PNG_IMAGE_SIZE(image));
    if (!bmp) { png_image_free(&image); return NULL; }

    bmp->width            = image.width;
    bmp->height           = image.height;
    bmp->bits_per_channel = BITMAP_BITS_PER_CHANNEL;
    bmp->bits_per_pixel   = BITMAP_BITS_PER_PIXEL;
    bmp->channel_count    = BITMAP_CHANNEL_COUNT;

    if (!png_image_finish_read(&image, NULL, bmp->pixel, 0, NULL)) {
        free(bmp);
        return NULL;
    }

    /* ── 2. Decode JABCode from bitmap ── */
    jab_int32 decode_status;
    jab_decoded_symbol symbols[MAX_SYMBOL_NUMBER];
    jab_data* decoded = decodeJABCodeEx(
        bmp, NORMAL_DECODE, &decode_status, symbols, MAX_SYMBOL_NUMBER);

    free(bmp);

    if (!decoded) return NULL;

    /* ── 3. Copy decoded payload into a plain malloc'd buffer ──
     *
     * We copy rather than hand the jab_data* directly to JS because
     * jab_data has a length prefix before the flex array, and the JS
     * side just wants a flat Uint8Array.
     */
    uint8_t* out = (uint8_t*)malloc(decoded->length);
    if (!out) { free(decoded); return NULL; }

    memcpy(out, decoded->data, decoded->length);
    *out_data_len = decoded->length;
    free(decoded);
    return out;
}

/* ── Heap release ─────────────────────────────────────────────────────────── */

/*
 * Release a buffer returned by cf_encode or cf_decode.
 * Must be called from JS once the data has been copied into a typed array.
 */
void cf_free(uint8_t* ptr)
{
    free(ptr);
}