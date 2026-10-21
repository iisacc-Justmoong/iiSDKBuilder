#pragma once
#include <iiSDKCore/Core/Result/Result.h>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace ii::builder::detail {
// Private JSON boundary for manifests, CMake File API replies and reports.
// Kept out of the SDK API; no dependency on a particular JSON library.
struct Json final {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value = nullptr;
    Json() = default;
    Json(std::nullptr_t) : value(nullptr) {}
    Json(bool v) : value(v) {}
    Json(double v) : value(v) {}
    Json(int v) : value(static_cast<double>(v)) {}
    Json(unsigned v) : value(static_cast<double>(v)) {}
    Json(std::string v) : value(std::move(v)) {}
    Json(const char* v) : value(std::string(v)) {}
    Json(std::string_view v) : value(std::string(v)) {}
    Json(Array v) : value(std::move(v)) {}
    Json(Object v) : value(std::move(v)) {}
    const Json& at(std::string_view key) const;
    bool has(std::string_view key) const;
    const Object& object() const;
    const Array& array() const;
    const std::string& string() const;
    unsigned integer() const;
};
core::Result<Json> parse_json(std::string_view text);
std::string dump_json(const Json& value);
core::Result<Json> read_json(const std::filesystem::path& file);
core::Result<void> write_text(const std::filesystem::path& file, std::string_view text);
std::string path_string(const std::filesystem::path& path);
std::filesystem::path utf8_path(std::string_view text);
} // namespace ii::builder::detail
