#pragma once
#include <filesystem>
#include <iiSDKCore/Core/Result/Result.h>
#include <string>
#include <vector>
namespace ii::builder::detail {
// Reads dylink.0 or legacy dylink metadata without executing a Wasm module.
core::Result<std::vector<std::string>> wasm_needed(const std::filesystem::path& file);
}
