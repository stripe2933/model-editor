#include "AssetExternalBuffers.hpp"

#include <meshoptimizer.h>

#include "../utils/ranges.hpp"
#include "../utils/utf8.hpp"

gltf::AssetExternalBuffers::AssetExternalBuffers(const fastgltf::Asset &asset, const std::filesystem::path &directory)
    : bufferViewBytes(asset.bufferViews.size()) {
    const auto getBufferBytes = [&](std::size_t bufferIndex) {
        return visit(fastgltf::visitor {
            [&](const fastgltf::sources::URI &uriSource) {
                auto it = mappedBufferSources.find(bufferIndex);
                if (it == mappedBufferSources.end()) {
                    it = mappedBufferSources.emplace_hint(it, bufferIndex, boost::iostreams::mapped_file_source {
#ifdef _WIN32
                        utils::toUTF8NFCString(directory / uriSource.uri.fspath()),
#else
                        directory / uriSource.uri.fspath(),
#endif
                        boost::iostreams::mapped_file_source::max_length,
                        static_cast<boost::intmax_t>(uriSource.fileByteOffset),
                    });
                }
                return std::span { reinterpret_cast<const std::byte*>(it->second.data()), it->second.size() };
            },
            [](const fastgltf::sources::Array &arraySource) {
                return std::span { arraySource.bytes };
            },
            [] [[noreturn]] (const auto&) -> std::span<const std::byte> {
                throw std::runtime_error { "Unsupported buffer source type" };
            },
        }, asset.buffers[bufferIndex].data);
    };

    for (auto [bufferViewIndex, bufferView] : ranges::views::enumerate(asset.bufferViews)) {
        if (const auto &mc = bufferView.meshoptCompression) {
            const auto compressed = reinterpret_cast<const unsigned char*>(getBufferBytes(mc->bufferIndex).subspan(mc->byteOffset).data());

            const std::size_t decompressedBufferSize = mc->count * mc->byteStride;
            std::byte* const decompressed = meshoptDecompressedBytes.emplace_back(std::make_unique_for_overwrite<std::byte[]>(decompressedBufferSize)).get();

            int result = -1;
            switch (mc->mode) {
                case fastgltf::MeshoptCompressionMode::Attributes:
                    result = meshopt_decodeVertexBuffer(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
                case fastgltf::MeshoptCompressionMode::Triangles:
                    result = meshopt_decodeIndexBuffer(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
                case fastgltf::MeshoptCompressionMode::Indices:
                    result = meshopt_decodeIndexSequence(decompressed, mc->count, mc->byteStride, compressed, mc->byteLength);
                    break;
            }

            if (result != 0) {
                throw std::runtime_error { "Failed to decompress EXT_meshopt_compression compressed buffer view." };
            }

            switch (mc->filter) {
                case fastgltf::MeshoptCompressionFilter::None:
                    break;
                case fastgltf::MeshoptCompressionFilter::Octahedral:
                    meshopt_decodeFilterOct(decompressed, mc->count, mc->byteStride);
                    break;
                case fastgltf::MeshoptCompressionFilter::Quaternion:
                    meshopt_decodeFilterQuat(decompressed, mc->count, mc->byteStride);
                    break;
                case fastgltf::MeshoptCompressionFilter::Exponential:
                    meshopt_decodeFilterExp(decompressed, mc->count, mc->byteStride);
                    break;
            }

            bufferViewBytes[bufferViewIndex] = { decompressed, decompressedBufferSize };
        }
        else {
            const std::span bufferBytes = getBufferBytes(bufferView.bufferIndex);
            bufferViewBytes[bufferViewIndex] = bufferBytes.subspan(bufferView.byteOffset, bufferView.byteLength);
        }
    }
}
