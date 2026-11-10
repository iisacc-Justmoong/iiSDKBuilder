#pragma once

#include <filesystem>
#include <iiSDKCore/Core/Result/Result.h>

namespace ii::builder::detail {
enum class CoffLibraryKind { import_library, static_library, mixed };
// Rejects truncated, non-COFF, and ambiguous inputs instead of guessing.
core::Result<CoffLibraryKind> inspect_coff_library(const std::filesystem::path& file);
}
