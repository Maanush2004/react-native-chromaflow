#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <png.h>
#include "jabcode.h"
#include "encoder.h"   /* character_size[], latch_shift_to[][], ecclevel2wcwr[],
                          getSymbolCapacity(), InitSymbols()                     */

/*
 * chromaflow_wasm.c — thin WASM-facing wrappers around libjabcode.
 *
 * Exported functions (called from JS via cwrap):
 *
 *   uint8_t* cf_encode(const uint8_t* data,    int data_len,
 *                      int color_number,        int module_size,
 *                      int symbol_width,        int symbol_height,
 *                      int ecc_level,
 *                      int symbol_version_x,    int symbol_version_y,
 *                      int* out_png_len)
 *
 *   uint8_t* cf_decode(const uint8_t* png_data, int png_len,
 *                      int* out_data_len)
 *
 *   int      cf_get_max_capacity(int color_number,
 *                                int module_size,
 *                                int symbol_width,        int symbol_height,
 *                                int ecc_level,
 *                                int symbol_version_x,    int symbol_version_y)
 *
 *   void     cf_free(uint8_t* ptr)
 *
 * Both encode and decode return a malloc'd buffer that the caller must
 * release with cf_free() once it has copied the data into a JS typed array.
 * NULL is returned on failure; out_*_len is set to 0 in that case.
 *
 * cf_get_max_capacity returns the maximum number of raw bytes that can be
 * encoded in a single JABCode frame with the given parameters, or 0 on
 * failure. Call this before cf_encode to size your payload correctly.
 *
 * Compile additions (append to the existing emmake line):
 *   -sEXPORTED_FUNCTIONS="['_cf_encode','_cf_decode','_cf_free','_cf_get_max_capacity','_malloc','_free']"
 *   -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap']"
 */

/* ── Function definitions ──────────────────────────────────────────────────── */
/* Definitions are from jabcode repo, src/jabcode/encoder.c */
jab_boolean InitSymbols(jab_encode* enc);
jab_int32 getSymbolCapacity(jab_encode* enc, jab_int32 index);

/* ── Capacity ─────────────────────────────────────────────────────────────── */

/*
 * Return the maximum number of raw bytes that fit in one JABCode frame.
 *
 * The function mirrors the capacity accounting in fitDataIntoSymbols()
 * but without any actual data — it computes the net bit capacity, subtracts
 * all structural overhead (flag bit, metadata S field, slave metadata), then
 * converts the remaining bits to bytes under worst-case byte-mode encoding.
 *
 * Byte-mode encoding costs (all values read from encoder.h constants):
 *
 *   shift_bits    = latch_shift_to[0][13]
 *                   Row 0:   currently in uppercase (JABCode always starts here)
 *                   Col 13:  shifting TO byte mode — col = 7 + 6 = 13 because
 *                            cols 0-6 are latches and cols 7-13 are shifts,
 *                            and byte mode is mode index 6.
 *                            latch_shift_to[0][6] = ENC_MAX so a direct latch
 *                            into byte mode from uppercase is not possible;
 *                            shift (col 13) is the only entry point.
 *
 *   header_bits   = 4 + 13 = 17  (from encodeData())
 *                   Always writes 4 bits for the byte count.
 *                   If count > 15, writes 13 more bits for the extended count.
 *                   We assume worst case (> 15 bytes) since payload size is
 *                   unknown at capacity-query time.
 *
 *   bits_per_char = character_size[6] = 8
 *                   character_size[] is indexed by mode: {5,5,4,4,5,6,8}
 *                   Index 6 is byte mode = 8 bits per raw byte.
 */
static jab_int32 getMaxDataCapacity(jab_encode* enc)
{
    jab_int32 capacity[enc->symbol_number];
    jab_int32 net_capacity[enc->symbol_number];
    jab_int32 total_net_capacity = 0;

    /* Calculate net capacity of each symbol after ECC overhead */
    for (jab_int32 i = 0; i < enc->symbol_number; i++)
    {
        capacity[i] = getSymbolCapacity(enc, i);
        enc->symbols[i].wcwr[0] = ecclevel2wcwr[enc->symbol_ecc_levels[i]][0];
        enc->symbols[i].wcwr[1] = ecclevel2wcwr[enc->symbol_ecc_levels[i]][1];
        net_capacity[i] = (capacity[i] / enc->symbols[i].wcwr[1]) * enc->symbols[i].wcwr[1]
                        - (capacity[i] / enc->symbols[i].wcwr[1]) * enc->symbols[i].wcwr[0];
        total_net_capacity += net_capacity[i];
    }

    /* Subtract per-symbol structural overhead to get bits available for data */
    jab_int32 total_data_capacity_bits = 0;
    for (jab_int32 i = 0; i < enc->symbol_number; i++)
    {
        jab_int32 overhead = 0;

        /* 1 flag bit per symbol (always present) */
        overhead += 1;

        /* Host metadata S field: 4 bits for master symbol, 3 bits for slaves */
        overhead += (i == 0) ? 4 : 3;

        /* Each slave symbol's metadata is embedded in its host's payload */
        for (jab_int32 j = 0; j < 4; j++)
        {
            if (enc->symbols[i].slaves[j] > 0)
                overhead += enc->symbols[enc->symbols[i].slaves[j]].metadata->length;
        }

        jab_int32 usable_bits = net_capacity[i] - overhead;
        if (usable_bits < 0) usable_bits = 0;
        total_data_capacity_bits += usable_bits;
    }

    /* Convert bit capacity to raw bytes under worst-case byte-mode encoding */
    jab_int32 shift_bits    = latch_shift_to[0][13]; /* 11: uppercase -> byte shift  */
    jab_int32 header_bits   = 4 + 13;                /* 17: byte count header        */
    jab_int32 bits_per_char = character_size[6];     /*  8: bits per byte in byte mode */

    jab_int32 overhead_bits = shift_bits + header_bits;
    jab_int32 max_raw_bytes = (total_data_capacity_bits - overhead_bits) / bits_per_char;

    if (max_raw_bytes < 0) max_raw_bytes = 0;
    return max_raw_bytes;
}

/*
 * WASM-exported capacity query.
 *
 * Creates a minimal single-symbol encoder with the given parameters,
 * runs InitSymbols() to populate the metadata structures that the
 * capacity calculation depends on, then returns the raw byte limit.
 *
 * Returns 0 on any failure (bad parameters, allocation error, etc.).
 */
int cf_get_max_capacity(int color_number,
                        int module_size,
                        int symbol_width,
                        int symbol_height,
                        int ecc_level,
                        int symbol_version_x,
                        int symbol_version_y)
{
    jab_encode* enc = createEncode(color_number > 0 ? color_number : 8, 1);
    if (!enc) return 0;

    if (module_size      > 0) enc->module_size           = module_size;
    if (symbol_width     > 0) enc->master_symbol_width   = symbol_width;
    if (symbol_height    > 0) enc->master_symbol_height  = symbol_height;
    if (ecc_level        > 0) enc->symbol_ecc_levels[0]  = (jab_byte)ecc_level;
    if (symbol_version_x > 0) enc->symbol_versions[0].x = symbol_version_x;
    if (symbol_version_y > 0) enc->symbol_versions[0].y = symbol_version_y;

    /* InitSymbols must be called before capacity calculation —
     * it sets up the metadata structures that getMaxDataCapacity reads. */
    if (!InitSymbols(enc)) {
        destroyEncode(enc);
        return 0;
    }

    jab_int32 result = getMaxDataCapacity(enc);
    destroyEncode(enc);
    return (int)result;
}

/* ── Encoder ──────────────────────────────────────────────────────────────── */

/*
 * Encode raw bytes into a JABCode PNG, entirely in memory.
 *
 * Parameters mirror the subset of jabwriter CLI options that ChromaFlow
 * actually uses.  Pass 0 for symbol_width / symbol_height to let the
 * library choose.  ecc_level 0 uses the library default (3).
 * symbol_version_x / symbol_version_y set the side-version of the single
 * master symbol (each 1–32, matching --symbol-version x y in jabwriter);
 * pass 0 for either to leave it at the library default.
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
                   int            symbol_version_x,
                   int            symbol_version_y,
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

    if (module_size      > 0) enc->module_size           = module_size;
    if (symbol_width     > 0) enc->master_symbol_width   = symbol_width;
    if (symbol_height    > 0) enc->master_symbol_height  = symbol_height;
    if (ecc_level        > 0) enc->symbol_ecc_levels[0]  = (jab_byte)ecc_level;
    if (symbol_version_x > 0) enc->symbol_versions[0].x = symbol_version_x;
    if (symbol_version_y > 0) enc->symbol_versions[0].y = symbol_version_y;

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