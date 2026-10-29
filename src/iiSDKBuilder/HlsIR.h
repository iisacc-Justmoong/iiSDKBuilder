#pragma once
#include "Hls.h"
#include "Json.h"
#include <cstdint>
#include <iiSDKCore/Core/Time/Deadline.h>
#include <optional>
namespace ii::builder::detail::hls {
struct Type final {
    unsigned bits = 0;
    bool sign = false;
    bool boolean = false;
    bool operator==(const Type &) const = default;
};
struct Node final {
    Type type;
    std::string op, name;
    std::vector<unsigned> inputs;
    std::optional<std::uint64_t> constant;
};
struct Circuit final {
    std::vector<Node> nodes;
    std::vector<HlsPort> ports;
    std::vector<std::vector<unsigned>> parameters;
    Type result_type;
    unsigned result = 0, iterations = 0;
};
Circuit lower(const Json &, const HlsRequest &, std::string_view source, std::stop_token, core::Deadline);
std::string emit_rtl(const Circuit &, const HlsRequest &);
Json circuit_json(const Circuit &);
bool identifier(std::string_view);
} // namespace ii::builder::detail::hls
