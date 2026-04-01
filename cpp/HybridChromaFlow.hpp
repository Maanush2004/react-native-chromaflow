// cpp/HybridChromaFlow.hpp
#pragma once
#include "HybridChromaFlowSpec.hpp"

namespace margelo::nitro::chromaflow {

class HybridChromaFlow : public HybridChromaFlowSpec {
public:
    HybridChromaFlow() : HybridObject(TAG) {}

    std::shared_ptr<ArrayBuffer> encode(
        const std::shared_ptr<ArrayBuffer>& data,
        double colorNumber,
        double moduleSize,
        double symbolWidth,
        double symbolHeight,
        double eccLevel
    ) override;

    std::shared_ptr<ArrayBuffer> decode(
        const std::shared_ptr<ArrayBuffer>& pngData
    ) override;

    std::shared_ptr<ArrayBuffer> decodeRaw(
        uint64_t buffer
    ) override;
};

} // namespace margelo::nitro::chromaflow