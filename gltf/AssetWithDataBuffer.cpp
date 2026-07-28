#include "AssetWithDataBuffer.hpp"

namespace {
    thread_local fastgltf::Parser parser {
        fastgltf::Extensions::KHR_texture_transform
            | fastgltf::Extensions::KHR_mesh_quantization
            | fastgltf::Extensions::EXT_meshopt_compression
            | fastgltf::Extensions::KHR_materials_emissive_strength
            | fastgltf::Extensions::KHR_materials_ior
            | fastgltf::Extensions::KHR_materials_unlit
            | fastgltf::Extensions::KHR_texture_basisu
            | fastgltf::Extensions::EXT_mesh_gpu_instancing
            | fastgltf::Extensions::EXT_texture_webp
    };

    template <typename T>
    [[nodiscard]] T unwrap(fastgltf::Expected<T> &&expected) {
        if (fastgltf::Error error = expected.error(); error != fastgltf::Error::None) {
            throw std::runtime_error { std::string { getErrorMessage(error) } };
        }

        return std::move(expected.get());
    }
}

gltf::AssetWithDataBuffer::AssetWithDataBuffer(std::filesystem::path path_)
    : path { std::move(path_) }
    , internalBuffer { unwrap(fastgltf::MappedGltfFile::FromPath(path)) }
    , asset { unwrap(parser.loadGltf(internalBuffer, path.parent_path())) }
    , externalBuffers { asset, path.parent_path() } { }
