#pragma once
#include <iiSDKBuilder/Export.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <filesystem>
#include <vector>

namespace ii::builder {
// Finds source files that define the native iiSDK plugin entry point.
// The root is read only; results are sorted absolute paths. Symlinks fail
// discovery; build, test, documentation and hidden directories are excluded.
[[nodiscard]] IISDKBUILDER_EXPORT core::Result<std::vector<std::filesystem::path>>
discover_static_plugins(const std::filesystem::path& root);
// Finds Python source in an application's runtime source tree. Each returned
// file must pass the native subset compiler; no interpreter fallback is used.
[[nodiscard]] IISDKBUILDER_EXPORT core::Result<std::vector<std::filesystem::path>>
discover_static_python(const std::filesystem::path& root);
// Finds runtime code and prebuilt dynamic modules without a native converter.
[[nodiscard]] IISDKBUILDER_EXPORT core::Result<std::vector<std::filesystem::path>>
discover_uncompiled_runtime(const std::filesystem::path& root);
}
