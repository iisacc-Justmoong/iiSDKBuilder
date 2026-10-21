#include <iiSDKBuilder/Resources.h>
#include <iiSDKCore/Resource/Pack.h>
#include <iiSDKCore/Core/Text/Utf8.h>
#include <iiFileProviderNative.h>
#include "Json.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace ii::builder {
namespace {
namespace fs=std::filesystem;
using detail::Json;
void fields(const Json& value,std::initializer_list<std::string_view> allowed) {
    for (const auto& [key,unused]:value.object()) {
        (void)unused;
        if (std::ranges::find(allowed,key)==allowed.end()) throw std::invalid_argument("Unknown resource field: "+key);
    }
}
bool identifier(std::string_view name) {
    return !name.empty() && name.size()<=64 &&
        ((name.front()>='a' && name.front()<='z') || (name.front()>='A' && name.front()<='Z')) &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==name.npos &&
        name.find("__")==name.npos;
}
fs::path resource_path(const fs::path& value) {
    const auto result=fs::absolute(value).lexically_normal();
    const auto text=detail::path_string(result);
    if (!core::valid_utf8(text) || text.find_first_of("\r\n\0",0,3)!=text.npos)
        throw std::invalid_argument("Resource path is not valid single-line UTF-8");
    return result;
}
bool publish(const fs::path& file,std::string_view bytes) {
    if (fs::exists(file) && fs::file_size(file)==bytes.size() &&
        iiFileProvider::native::File::read(file,bytes.size())==bytes) return false;
    iiFileProvider::native::File::createDirectories(file.parent_path());
    iiFileProvider::native::File::write(file,bytes);return true;
}
std::string dep_escape(const fs::path& path) {
    const auto text=detail::path_string(path);std::string escaped;
    for (const auto c:text) {
        if (c=='$') escaped+="$$";
        else { if (c==' ' || c=='#' || c==':' || c=='\\') escaped+='\\';escaped+=c; }
    }return escaped;
}
}
core::Result<ResourceReport> compile_resources(const fs::path& manifest,Platform target,const ResourceOutputs& requested) {
    try {
        if (platform_name(target)=="unknown") throw std::invalid_argument("Unknown resource target platform");
        if (requested.cpp.empty()!=requested.header.empty() || (requested.cpp.empty() && requested.pack.empty()))
            throw std::invalid_argument("Specify both cpp/header outputs, a pack output, or both");
        const auto source_manifest=resource_path(manifest);
        auto parsed=detail::read_json(source_manifest);if (!parsed) return std::unexpected(parsed.error());
        const auto& root=*parsed;fields(root,{"schema","name","resources","maximumBytes"});
        if (root.at("schema").integer()!=1) throw std::invalid_argument("Resource manifest schema must be 1");
        ResourceReport report;report.name=root.at("name").string();
        if (!identifier(report.name)) throw std::invalid_argument("Resource name must be an ASCII identifier of at most 64 bytes");
        const auto maximum=root.has("maximumBytes")?root.at("maximumBytes").integer():512u*1024u*1024u;
        const auto& entries=root.at("resources").array();
        if (entries.size()>1'000'000) throw std::invalid_argument("Too many resource manifest entries");
        struct Variant final { std::string selector;fs::path source; };
        std::map<std::string,std::vector<Variant>> candidates;
        std::set<fs::path> protected_inputs{fs::weakly_canonical(source_manifest)};
        for (const auto& entry:entries) {
            fields(entry,{"name","source","platform"});
            const auto name=entry.at("name").string();
            if (name.size()>4096 || !resource::valid_name(name)) throw std::invalid_argument("Noncanonical or oversized resource name: "+name);
            const auto input=detail::utf8_path(entry.at("source").string());
            if (input.empty()) throw std::invalid_argument("Resource input path is empty: "+name);
            const auto file=resource_path(input.is_absolute()?input:source_manifest.parent_path()/input);
            protected_inputs.insert(fs::weakly_canonical(file));
            auto selector=entry.has("platform")?entry.at("platform").string():std::string{};
            if (!selector.empty() && !parse_platform(selector)) throw std::invalid_argument("Invalid resource platform: "+selector);
            auto& variants=candidates[name];
            if (std::ranges::any_of(variants,[&](const auto& v){return v.selector==selector;}))
                throw std::invalid_argument("Duplicate resource name/platform: "+name);
            variants.push_back({std::move(selector),file});
        }
        ResourceOutputs outputs=requested;
        std::set<fs::path> output_paths;
        for (auto* output:{&outputs.cpp,&outputs.header,&outputs.pack,&outputs.depfile}) {
            if (output->empty()) continue;
            *output=resource_path(*output);bool under_build=false;
            for (const auto& part:output->parent_path()) if (part=="build") under_build=true;
            if (!under_build) throw std::invalid_argument("Resource outputs must stay under a build/ directory");
            const auto canonical=fs::weakly_canonical(*output);
            if (protected_inputs.contains(canonical) || output_paths.contains(canonical))
                throw std::invalid_argument("Resource output would replace an input or another output");
            if (fs::exists(canonical)) for (const auto* group:{&protected_inputs,&output_paths}) for (const auto& input:*group) {
                std::error_code error;
                const auto present=fs::exists(input,error);
                if (error) throw fs::filesystem_error("Cannot inspect resource input/output identity",input,error);
                if (!present) continue;
                if (fs::equivalent(canonical,input,error))
                    throw std::invalid_argument("Resource outputs cannot alias inputs or each other");
                if (error && error!=std::errc::no_such_file_or_directory && error!=std::errc::not_a_directory)
                    throw fs::filesystem_error("Cannot verify resource input/output identity",canonical,input,error);
            }
            output_paths.insert(canonical);
        }
        if (!outputs.header.empty() && detail::path_string(outputs.header.filename()).find_first_of("\"\\")!=std::string::npos)
            throw std::invalid_argument("Resource header filename cannot contain quotes or backslashes");
        if (!outputs.cpp.empty() && outputs.cpp.parent_path()!=outputs.header.parent_path())
            throw std::invalid_argument("Generated C++ and header must share their output directory");
        std::vector<std::pair<std::string,fs::path>> selected;
        for (const auto& [name,variants]:candidates) {
            const Variant* choice=nullptr;
            for (const auto& v:variants) {
                if (v.selector==platform_name(target)) { choice=&v;break; }
                if (v.selector.empty()) choice=&v;
            }
            if (choice) selected.emplace_back(name,choice->source);
        }
        std::vector<std::string> content;content.reserve(selected.size());
        std::vector<resource::Source> sources;sources.reserve(selected.size());
        std::uint64_t input_bytes=0;report.dependencies.push_back(source_manifest);
        for (const auto& [name,file]:selected) {
            content.push_back(iiFileProvider::native::File::read(file,maximum-input_bytes));
            input_bytes+=content.back().size();
            if (input_bytes>maximum) throw std::invalid_argument("Selected resource input byte budget exceeded");
            sources.push_back({name,core::byte_view(content.back())});report.dependencies.push_back(file);
        }
        auto encoded=resource::build(sources,{.maximum_bytes=maximum});
        if (!encoded) return std::unexpected(encoded.error());
        auto checked=resource::Pack::copy(*encoded);
        if (!checked) return std::unexpected(checked.error());
        const auto stats=checked->stats();
        report.entries=stats.entries;report.unique_payloads=stats.unique_payloads;
        report.logical_bytes=stats.logical_bytes;report.stored_bytes=stats.stored_bytes;
        std::string header,cpp,depfile;
        if (!outputs.cpp.empty()) {
            if (encoded->size()>32*1024*1024) throw std::invalid_argument("Embedded packs are limited to 32 MiB; use a mapped pack for larger assets");
            header="#pragma once\n#include <iiSDKCore/Resource/Pack.h>\nnamespace ii::resource::embedded {\n"
                "ii::core::Result<ii::resource::Pack> load_"+report.name+"();\n}\n";
            cpp="#include \""+detail::path_string(outputs.header.filename())+"\"\n#include <span>\n"
                "namespace {\nalignas(ii::resource::PackAlignment) const unsigned char data[] = {";
            constexpr char hex[]="0123456789abcdef";
            cpp.reserve(cpp.size()+encoded->size()*6+512);
            for (std::size_t i=0;i<encoded->size();++i) {
                if (i%16==0) cpp+="\n ";
                const auto byte=std::to_integer<unsigned char>((*encoded)[i]);
                cpp+="0x";cpp+=hex[byte>>4];cpp+=hex[byte&15];cpp+=',';
            }
            cpp+="\n};\n}\nnamespace ii::resource::embedded {\nii::core::Result<ii::resource::Pack> load_"+report.name+
                "() {\n static const auto pack=ii::resource::Pack::open_static(std::as_bytes(std::span(data)));\n return pack;\n}\n}\n";
        }
        std::ranges::sort(report.dependencies);
        report.dependencies.erase(std::unique(report.dependencies.begin(),report.dependencies.end()),report.dependencies.end());
        if (!outputs.depfile.empty()) {
            depfile=dep_escape(outputs.cpp.empty()?outputs.pack:outputs.cpp)+":";
            for (const auto& input:report.dependencies) depfile+=" "+dep_escape(input);
            depfile+='\n';
        }
        // Complete validation/encoding before publishing any artifact.
        if (!outputs.header.empty()) report.changed=publish(outputs.header,header)||report.changed;
        if (!outputs.cpp.empty()) report.changed=publish(outputs.cpp,cpp)||report.changed;
        if (!outputs.pack.empty()) report.changed=publish(outputs.pack,
            {reinterpret_cast<const char*>(encoded->data()),encoded->size()})||report.changed;
        if (!outputs.depfile.empty()) report.changed=publish(outputs.depfile,depfile)||report.changed;
        return report;
    } catch (const std::bad_alloc&) {
        return core::fail(core::ErrorCode::failed,"Resource compiler allocation failed",std::current_exception());
    } catch (const iiFileProvider::native::FileError& error) {
        return core::fail(core::ErrorCode::failed,"Resource file: "+std::string(error.what()),std::current_exception());
    } catch (const fs::filesystem_error& error) {
        return core::fail(core::ErrorCode::failed,"Resource filesystem: "+std::string(error.what()),std::current_exception());
    } catch (const std::exception& error) {
        return core::fail(core::ErrorCode::invalid_argument,"Resource compiler: "+std::string(error.what()),std::current_exception());
    }
}
} // namespace ii::builder
