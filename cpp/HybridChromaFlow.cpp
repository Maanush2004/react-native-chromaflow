#include "HybridChromaFlow.hpp"
#include <android/hardware_buffer.h>

extern "C" {
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

std::string HybridChromaFlow::describeBuffer(uint64_t pointer)
{
  AHardwareBuffer* buffer = reinterpret_cast<AHardwareBuffer*>(pointer);
  AHardwareBuffer_Desc desc;
  AHardwareBuffer_describe(buffer, &desc);

  std::string format;
  switch (desc.format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:  format = "RGBA8";    break;
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:  format = "RGBX8";    break;
    case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:    format = "RGB8";      break;
    case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:    format = "RGB565";    break;
    case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:    format = "YUV420";    break;
    case AHARDWAREBUFFER_FORMAT_YCbCr_P010:      format = "YUV_P010";  break;
    default: format = "unknown(" + std::to_string(desc.format) + ")"; break;
  }

  return "format=" + format +
         " width="  + std::to_string(desc.width)  +
         " height=" + std::to_string(desc.height) +
         " stride=" + std::to_string(desc.stride) +
         " layers=" + std::to_string(desc.layers);
}

} // namespace margelo::nitro::chromaflow