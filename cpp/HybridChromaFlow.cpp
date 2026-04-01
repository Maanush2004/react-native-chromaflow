#include "HybridChromaFlow.hpp"
#include <android/hardware_buffer.h>

extern "C" {
    #include "jabcode.h"
    uint8_t* cf_encode(const uint8_t* data,
                       int data_len,
                       int color_number,
                       int module_size,
                       int symbol_width,
                       int symbol_height,
                       int ecc_level,
                       int* out_png_len);

    uint8_t* cf_decode(const uint8_t* png_data,
                       int png_len,
                       int* out_data_len);

    void cf_free(uint8_t* ptr);

    jab_data* decodeJABCodeEx(jab_bitmap* bitmap, jab_int32 mode, jab_int32* status, jab_decoded_symbol* symbols, jab_int32 max_symbol_number);
}

namespace margelo::nitro::chromaflow {

std::shared_ptr<ArrayBuffer> HybridChromaFlow::encode(
    const std::shared_ptr<ArrayBuffer>& data,
    double colorNumber,
    double moduleSize,
    double symbolWidth,
    double symbolHeight,
    double eccLevel)
{
    int pngLen = 0;
    uint8_t* buf = cf_encode(
        data->data(),
        (int)data->size(),
        (int)colorNumber,
        (int)moduleSize,
        (int)symbolWidth,
        (int)symbolHeight,
        (int)eccLevel,
        &pngLen
    );

    if (!buf) {
        throw std::runtime_error("JABCode encode failed");
    }

    return ArrayBuffer::wrap(
        buf,
        (size_t)pngLen,
        [buf]() { cf_free(buf); }
    );
}

std::shared_ptr<ArrayBuffer> HybridChromaFlow::decode(
    const std::shared_ptr<ArrayBuffer>& pngData)
{
    int outLen = 0;
    uint8_t* buf = cf_decode(
        pngData->data(),
        (int)pngData->size(),
        &outLen
    );

    if (!buf) {
        throw std::runtime_error("JABCode decode failed");
    }

    return ArrayBuffer::wrap(
        buf,
        (size_t)outLen,
        [buf]() { cf_free(buf); }
    );
}

std::shared_ptr<ArrayBuffer> HybridChromaFlow::decodeRaw(uint64_t buffer) {
    AHardwareBuffer* bfr = reinterpret_cast<AHardwareBuffer*>(buffer);
    if (!bfr) {
        throw std::runtime_error("Buffer is NULL");
    }

    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(bfr, &desc);

    int bpp = 0;
    int bpc = 8; // bits per channel, always 8 for these formats
    int bytesPerPixel = 0; // Will be assigned as per the pixel format below
    int channels = 0;
    switch (desc.format) {
        case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
        case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
            bpp = 32; // bits per pixel
            channels = 4;
            bytesPerPixel = 4;
            break;
        case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
            bpp = 24;
            channels = 3;
            bytesPerPixel = 3;
            break;
        default:
            // The JABCODE library only deals with, R8, G8 and B8 channels.
            throw std::runtime_error("Unsupported buffer format (only RGB allowed)");
    }

    // Lock for reading
    void* data = nullptr;
    int res = AHardwareBuffer_lock(
        bfr,
        AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
        -1,
        nullptr,
        &data
    );
    if (res != 0 || !data) {
        throw std::runtime_error("Failed to lock AHardwareBuffer");
    }

    uint8_t* base = static_cast<uint8_t*>(data);
    size_t rowBytes  = (size_t)desc.width * bytesPerPixel;
    size_t pixelsSize = rowBytes * desc.height;

    // Allocate jab_bitmap + pixel data in one contiguous block
    size_t totalSize = sizeof(jab_bitmap) + pixelsSize;
    jab_bitmap* bitmap = reinterpret_cast<jab_bitmap*>(malloc(totalSize));
    if (!bitmap) {
        AHardwareBuffer_unlock(bfr, nullptr);
        throw std::runtime_error("Failed to allocate jab_bitmap");
    }

    // Fill header
    bitmap->width           = (jab_int32)desc.width;
    bitmap->height          = (jab_int32)desc.height;
    bitmap->bits_per_pixel  = (jab_int32)bpp;
    bitmap->bits_per_channel= (jab_int32)bpc;
    bitmap->channel_count   = (jab_int32)channels;

    // Copy row by row to strip stride padding
    for (uint32_t y = 0; y < desc.height; y++) {
        const uint8_t* src = base + y * desc.stride * bytesPerPixel;
        jab_byte*      dst = bitmap->pixel + y * rowBytes;
        memcpy(dst, src, rowBytes);
    }

    AHardwareBuffer_unlock(bfr, nullptr);

    //Decode JABCode from bitmap
    jab_int32 decode_status;
    jab_decoded_symbol symbols[MAX_SYMBOL_NUMBER];
    jab_data* decoded = decodeJABCodeEx(
        bitmap, NORMAL_DECODE, &decode_status, symbols, MAX_SYMBOL_NUMBER);

    free(bitmap);

    if (!decoded) {
        throw std::runtime_error("JABCODE decode failed");
    }

    return ArrayBuffer::wrap(
        reinterpret_cast<uint8_t*>(decoded->data),
        (size_t)decoded->length,
        [decoded]() { free(decoded); }
    );
    
}

} // namespace margelo::nitro::chromaflow