#pragma once

#include "../pch.hpp"

#include "AssetExternalBuffers.hpp"

namespace gltf {
    class AssetWithDataBuffer {
    public:
        std::filesystem::path path;

    private:
        fastgltf::MappedGltfFile internalBuffer;

    public:
        fastgltf::Asset asset;
        AssetExternalBuffers externalBuffers;

        AssetWithDataBuffer(std::filesystem::path path);
    };
}
