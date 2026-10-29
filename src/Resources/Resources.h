#pragma once
#include <iiSDKBuilder/Builder.h>
#include <cstdint>

namespace ii::builder {
struct ResourceOutputs final {
    std::filesystem::path cpp, header, pack, depfile;
};
struct ResourceReport final {
    std::string name;
    std::size_t entries = 0, unique_payloads = 0, stored_bytes = 0;
    std::uint64_t logical_bytes = 0;
    bool changed = false;
    std::vector<std::filesystem::path> dependencies;
};
// Host-side compilation of immutable Core packs. Select target variants before
// opening files. All publication uses iiFileProvider, never a Core dependency.
[[nodiscard]] IISDKBUILDER_EXPORT core::Result<ResourceReport> compile_resources(
    const std::filesystem::path& manifest,Platform target,const ResourceOutputs&);
} // namespace ii::builder
