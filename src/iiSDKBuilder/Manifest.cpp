#include <iiSDKBuilder/Builder.h>
#include "Json.h"
#include "Targets.h"
#include <algorithm>
#include <stdexcept>

namespace ii::builder {
namespace {
using detail::Json;
namespace fs = std::filesystem;
void fields(const Json& value, std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, unused]:value.object()) {
        (void)unused;
        if (std::ranges::find(allowed,key)==allowed.end()) throw std::runtime_error("Unknown manifest field: "+key);
    }
}
std::string text(const Json& j,std::string_view key,std::string fallback={}) {
    return j.has(key)?j.at(key).string():std::move(fallback);
}
unsigned number(const Json& j,std::string_view key,unsigned fallback) {
    return j.has(key)?j.at(key).integer():fallback;
}
fs::path path(const Json& j,std::string_view key,const fs::path& base,fs::path fallback={}) {
    if (!j.has(key)) return fallback;
    const auto input=detail::utf8_path(j.at(key).string());
    if (input.empty()) return {};
    return (input.is_absolute()?input:base/input).lexically_normal();
}
std::vector<std::string> strings(const Json& j,std::string_view key) {
    std::vector<std::string> out;
    if (j.has(key)) for (const auto& item:j.at(key).array()) out.push_back(item.string());
    return out;
}
Variables variables(const Json& j,std::string_view key) {
    Variables out;
    if (j.has(key)) for (const auto& [name,value]:j.at(key).object()) out.emplace(name,value.string());
    return out;
}
CMakeProject project(const Json& j,const fs::path& base) {
    fields(j,{"name","source","depends","cache","targets"});
    return {j.at("name").string(),path(j,"source",base),strings(j,"depends"),variables(j,"cache"),strings(j,"targets")};
}
Json::Array json_strings(const std::vector<std::string>& items) {
    Json::Array out;
    for (const auto& item:items) out.emplace_back(item);
    return out;
}
Json json_variables(const Variables& variables) {
    Json::Object out;
    for (const auto& [key,value]:variables) out.emplace(key,value);
    return out;
}
std::string absolute_text(const fs::path& p) {
    return detail::path_string(p.empty()?p:fs::absolute(p).lexically_normal());
}
Json json_project(const CMakeProject& p) {
    return Json::Object{{"name",p.name},{"source",absolute_text(p.source)},
        {"depends",json_strings(p.depends)},{"cache",json_variables(p.cache)},{"targets",json_strings(p.targets)}};
}
}

core::Result<Request> read_manifest(const fs::path& file) {
    auto root=detail::read_json(file);
    if (!root) return std::unexpected(root.error());
    try {
        fields(*root,{"schema","app","sdkRoot","sdks","kits","buildRoot","cmake","configuration","jobs","parallelKits","timeoutSeconds","deploy","run"});
        if (root->at("schema").integer()!=1) throw std::runtime_error("Unsupported iiSDKBuilder manifest schema (expected 1)");
        const auto base=fs::absolute(file).parent_path().lexically_normal();
        Request r;
        r.app=project(root->at("app"),base);
        r.sdk_root=path(*root,"sdkRoot",base);
        r.build_root=path(*root,"buildRoot",base);
        if (root->has("cmake")) {
            auto cmake=detail::utf8_path(root->at("cmake").string());
            r.cmake=cmake.has_parent_path()?(cmake.is_absolute()?cmake:base/cmake).lexically_normal():cmake;
        }
        r.configuration=text(*root,"configuration","Release");
        r.jobs=number(*root,"jobs",2);
        r.parallel_kits=number(*root,"parallelKits",2);
        r.timeout=std::chrono::seconds{number(*root,"timeoutSeconds",1800)};
        for(auto item:{std::pair{"deploy",&r.deploy},std::pair{"run",&r.run}})if(root->has(item.first)){
            const auto* value=std::get_if<bool>(&root->at(item.first).value);
            if(!value)throw std::runtime_error(std::string(item.first)+" must be boolean");*item.second=*value;
        }
        if (root->has("sdks")) for (const auto& item:root->at("sdks").array()) r.sdks.push_back(project(item,base));
        if (root->has("kits")) {
            r.kits.clear();
            for (const auto& item:root->at("kits").array()) {
                fields(item,{"id","platform","toolchain","generator","architecture","sysroot","androidApi","androidAbi","cache","environment","prefixes","target"});
                Kit kit;
                kit.id=item.at("id").string();
                auto platform=parse_platform(item.at("platform").string());
                if (!platform) return std::unexpected(platform.error());
                kit.platform=*platform;
                kit.toolchain=path(item,"toolchain",base);
                kit.generator=text(item,"generator");
                kit.architecture=text(item,"architecture");
                kit.sysroot=text(item,"sysroot");
                if (const auto p=detail::utf8_path(kit.sysroot); p.has_parent_path() && p.is_relative())
                    kit.sysroot=detail::path_string((base/p).lexically_normal());
                kit.android_api=number(item,"androidApi",24);
                kit.android_abi=text(item,"androidAbi","arm64-v8a");
                kit.cache=variables(item,"cache");
                kit.environment=variables(item,"environment");
                if (item.has("prefixes")) for (const auto& prefix:item.at("prefixes").array()) {
                    auto p=detail::utf8_path(prefix.string());
                    kit.prefixes.push_back((p.is_absolute()?p:base/p).lexically_normal());
                }
                if(item.has("target"))kit.target=detail::read_target(item.at("target"),base);
                r.kits.push_back(std::move(kit));
            }
        }
        return r;
    } catch (const std::exception& e) {
        return core::fail(core::ErrorCode::invalid_argument,"Manifest "+detail::path_string(file)+": "+e.what());
    }
}

core::Result<void> write_manifest(const Request& r,const fs::path& file) {
    Json::Array sdks,kits;
    for (const auto& sdk:r.sdks) sdks.push_back(json_project(sdk));
    for (const auto& k:r.kits) {
        Json::Array prefixes;
        for (const auto& p:k.prefixes) prefixes.emplace_back(absolute_text(p));
        kits.emplace_back(Json::Object{{"id",k.id},{"platform",platform_name(k.platform)},
            {"toolchain",absolute_text(k.toolchain)},{"generator",k.generator},
            {"architecture",k.architecture},{"sysroot",k.sysroot},{"androidApi",k.android_api},
            {"androidAbi",k.android_abi},{"cache",json_variables(k.cache)},
            {"environment",json_variables(k.environment)},{"prefixes",std::move(prefixes)},{"target",detail::target_json(k.target)}});
    }
    return detail::write_text(file,detail::dump_json(Json::Object{{"schema",1},{"app",json_project(r.app)},
        {"sdkRoot",absolute_text(r.sdk_root)},{"sdks",std::move(sdks)},{"kits",std::move(kits)},
        {"buildRoot",absolute_text(r.build_root)},
        {"cmake",r.cmake.has_parent_path()?absolute_text(r.cmake):detail::path_string(r.cmake)},
        {"configuration",r.configuration},{"jobs",r.jobs},{"parallelKits",r.parallel_kits},
        {"timeoutSeconds",static_cast<double>(r.timeout.count())},{"deploy",r.deploy},{"run",r.run}}));
}

std::string plan_json(const Plan& plan) {
    Json::Array kits;
    for (const auto& k:plan.kits) {
        Json::Array projects;
        for (const auto& p:k.projects) {
            Json::Array commands;
            for (const auto& command:p.commands) commands.emplace_back(Json::Object{
                {"phase",phase_name(command.phase)},{"arguments",json_strings(command.arguments)},
                {"log",detail::path_string(command.log)}});
            projects.emplace_back(Json::Object{{"name",p.project.name},{"source",detail::path_string(p.project.source)},
                {"hostValidation",p.host_validation},{"binaryDir",detail::path_string(p.binary_dir)},{"commands",std::move(commands)}});
        }
        kits.emplace_back(Json::Object{{"id",k.kit.id},{"platform",platform_name(k.kit.platform)},
            {"executionModel",execution_model_name(execution_model(k.kit))},{"profile",k.kit.target.profile},{"target",detail::target_json(k.kit.target)},
            {"available",k.unavailable_reason.empty()},{"reason",k.unavailable_reason},
            {"toolchain",detail::path_string(k.kit.toolchain)},{"prefix",detail::path_string(k.prefix)},
            {"projects",std::move(projects)}});
    }
    return detail::dump_json(Json::Object{{"schema",1},{"buildRoot",detail::path_string(plan.request.build_root)},
        {"configuration",plan.request.configuration},{"kits",std::move(kits)}});
}
} // namespace ii::builder
