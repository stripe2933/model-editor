#pragma once

#include "../pch.hpp"

namespace gltf {
    class AssetExternalBuffers {
    public:
        AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &directory);

        [[nodiscard]] std::span<const std::byte> operator()(const fastgltf::Asset&, std::size_t bufferViewIndex) const noexcept {
            return getBufferViewBytes(bufferViewIndex);
        }

        [[nodiscard]] std::span<const std::byte> getBufferViewBytes(std::size_t bufferViewIndex) const noexcept {
            return bufferViewBytes[bufferViewIndex];
        }

    private:
        std::unordered_map<std::size_t /* buffer index */, boost::iostreams::mapped_file_source> mappedBufferSources;
        std::vector<std::unique_ptr<std::byte[]>> meshoptDecompressedBytes;
        std::vector<std::span<const std::byte>> bufferViewBytes;
    };
}