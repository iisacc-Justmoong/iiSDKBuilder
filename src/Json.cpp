#include <iiFileProviderStorage.h>
#include "Json.h"
#include <iiFileProviderNative.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace ii::builder::detail {
namespace {
[[noreturn]] void invalid(const std::string& message) { throw std::runtime_error(message); }
constexpr std::size_t max_bytes = 16 * 1024 * 1024;
void validate_utf8(std::string_view text) {
    for (std::size_t i=0;i<text.size();) {
        const auto first=static_cast<unsigned char>(text[i++]);
        if (first<0x80) continue;
        unsigned count=0,cp=0,minimum=0;
        if (first>=0xC2 && first<=0xDF) { count=1;cp=first&31;minimum=0x80; }
        else if (first>=0xE0 && first<=0xEF) { count=2;cp=first&15;minimum=0x800; }
        else if (first>=0xF0 && first<=0xF4) { count=3;cp=first&7;minimum=0x10000; }
        else invalid("Invalid UTF-8 in JSON");
        for (unsigned n=0;n<count;++n) {
            if (i==text.size()) invalid("Truncated UTF-8 in JSON");
            const auto next=static_cast<unsigned char>(text[i++]);
            if ((next&0xC0)!=0x80) invalid("Invalid UTF-8 continuation in JSON");
            cp=(cp<<6)|(next&63);
        }
        if (cp<minimum || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) invalid("Invalid UTF-8 scalar in JSON");
    }
}
struct Reader final {
    std::string_view text;
    std::size_t pos = 0;
    void space() { while (pos < text.size() && (text[pos]==' ' || text[pos]=='\n' || text[pos]=='\r' || text[pos]=='\t')) ++pos; }
    char take() { if (pos == text.size()) invalid("Unexpected end of JSON"); return text[pos++]; }
    void expect(char c) { if (take() != c) invalid("Unexpected JSON character at byte " + std::to_string(pos)); }
    unsigned hex4() {
        unsigned value = 0;
        for (int i=0; i<4; ++i) {
            const char c=take();
            value <<= 4;
            if (c>='0' && c<='9') value += c-'0';
            else if (c>='a' && c<='f') value += c-'a'+10;
            else if (c>='A' && c<='F') value += c-'A'+10;
            else invalid("Invalid JSON Unicode escape");
        }
        return value;
    }
    void unicode(std::string& out) {
        auto cp = hex4();
        if (cp>=0xD800 && cp<=0xDBFF) {
            expect('\\'); expect('u');
            const auto low = hex4();
            if (low<0xDC00 || low>0xDFFF) invalid("Invalid JSON surrogate pair");
            cp=0x10000+((cp-0xD800)<<10)+(low-0xDC00);
        } else if (cp>=0xDC00 && cp<=0xDFFF) invalid("Unpaired JSON low surrogate");
        if (!cp) invalid("NUL is not allowed in builder JSON strings");
        if (cp<0x80) out+=static_cast<char>(cp);
        else if (cp<0x800) { out+=static_cast<char>(0xC0|(cp>>6)); out+=static_cast<char>(0x80|(cp&63)); }
        else if (cp<0x10000) { out+=static_cast<char>(0xE0|(cp>>12)); out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63)); }
        else { out+=static_cast<char>(0xF0|(cp>>18)); out+=static_cast<char>(0x80|((cp>>12)&63)); out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63)); }
    }
    std::string string() {
        expect('"');
        std::string out;
        for (;;) {
            const auto c=take();
            if (c=='"') return out;
            if (static_cast<unsigned char>(c)<0x20) invalid("Unescaped JSON control character");
            if (c!='\\') { out+=c; continue; }
            switch (take()) {
            case '"': out+='"'; break;
            case '\\': out+='\\'; break;
            case '/': out+='/'; break;
            case 'b': out+='\b'; break;
            case 'f': out+='\f'; break;
            case 'n': out+='\n'; break;
            case 'r': out+='\r'; break;
            case 't': out+='\t'; break;
            case 'u': unicode(out); break;
            default: invalid("Invalid JSON escape");
            }
        }
    }
    Json read(unsigned depth=0) {
        if (depth>64) invalid("JSON nesting exceeds 64 levels");
        space();
        if (pos==text.size()) invalid("Missing JSON value");
        const char c=text[pos];
        if (c=='"') return string();
        if (c=='{' || c=='[') {
            ++pos; space();
            Json::Object object;
            Json::Array array;
            const char end=c=='{'?'}':']';
            if (pos<text.size() && text[pos]==end) { ++pos; return c=='{'?Json{object}:Json{array}; }
            for (;;) {
                space();
                if (c=='{') {
                    auto key=string(); space(); expect(':');
                    auto value=read(depth+1);
                    if (!object.emplace(std::move(key),std::move(value)).second) invalid("Duplicate JSON key");
                } else array.push_back(read(depth+1));
                space();
                const char separator=take();
                if (separator==end) return c=='{'?Json{std::move(object)}:Json{std::move(array)};
                if (separator!=',') invalid("Expected JSON comma");
            }
        }
        for (const auto literal : {std::string_view{"true"},std::string_view{"false"},std::string_view{"null"}}) {
            if (text.substr(pos,literal.size())==literal) {
                pos+=literal.size();
                if (literal=="null") return {};
                return Json{literal=="true"};
            }
        }
        const auto start=pos;
        if (text[pos]=='-') ++pos;
        if (pos==text.size() || text[pos]<'0' || text[pos]>'9') invalid("Invalid JSON number");
        if (text[pos]=='0') ++pos;
        else while (pos<text.size() && text[pos]>='0' && text[pos]<='9') ++pos;
        if (pos<text.size() && text[pos]=='.') {
            ++pos; const auto first=pos;
            while (pos<text.size() && text[pos]>='0' && text[pos]<='9') ++pos;
            if (first==pos) invalid("Missing JSON fraction digits");
        }
        if (pos<text.size() && (text[pos]=='e' || text[pos]=='E')) {
            ++pos;
            if (pos<text.size() && (text[pos]=='+' || text[pos]=='-')) ++pos;
            const auto first=pos;
            while (pos<text.size() && text[pos]>='0' && text[pos]<='9') ++pos;
            if (first==pos) invalid("Missing JSON exponent digits");
        }
        double number=0;
        const auto converted=std::from_chars(text.data()+start,text.data()+pos,number);
        if (converted.ec!=std::errc{} || !std::isfinite(number)) invalid("JSON number out of range");
        return number;
    }
};
void quote(std::string& out, std::string_view input) {
    constexpr char hex[]="0123456789abcdef";
    out+='"';
    for (const unsigned char c:input) {
        if (c=='"' || c=='\\') { out+='\\'; out+=static_cast<char>(c); }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=static_cast<char>(c);
    }
    out+='"';
}
void dump(std::string& out,const Json& json) {
    std::visit([&](const auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::nullptr_t>) out+="null";
        else if constexpr (std::is_same_v<T,bool>) out+=v?"true":"false";
        else if constexpr (std::is_same_v<T,double>) {
            char buffer[64]; const auto result=std::to_chars(buffer,buffer+64,v);
            if (result.ec!=std::errc{} || !std::isfinite(v)) invalid("Cannot encode JSON number");
            out.append(buffer,result.ptr);
        } else if constexpr (std::is_same_v<T,std::string>) quote(out,v);
        else if constexpr (std::is_same_v<T,Json::Array>) {
            out+='['; bool first=true;
            for (const auto& element:v) { if (!first) out+=','; first=false; dump(out,element); }
            out+=']';
        } else {
            out+='{'; bool first=true;
            for (const auto& [key,value]:v) { if (!first) out+=','; first=false; quote(out,key); out+=':'; dump(out,value); }
            out+='}';
        }
    },json.value);
}
}
const Json& Json::at(std::string_view key) const {
    const auto& o=object(); const auto found=o.find(std::string(key));
    if (found==o.end()) invalid("Missing JSON field: "+std::string(key));
    return found->second;
}
bool Json::has(std::string_view key) const { return object().contains(std::string(key)); }
const Json::Object& Json::object() const {
    if (const auto* p=std::get_if<Object>(&value)) return *p;
    invalid("Expected JSON object");
}
const Json::Array& Json::array() const {
    if (const auto* p=std::get_if<Array>(&value)) return *p;
    invalid("Expected JSON array");
}
const std::string& Json::string() const {
    if (const auto* p=std::get_if<std::string>(&value)) return *p;
    invalid("Expected JSON string");
}
unsigned Json::integer() const {
    if (const auto* p=std::get_if<double>(&value); p && *p>=0 && *p<=std::numeric_limits<unsigned>::max() && std::floor(*p)==*p)
        return static_cast<unsigned>(*p);
    invalid("Expected unsigned JSON integer");
}
core::Result<Json> parse_json(std::string_view text) {
    try {
        if (text.size()>max_bytes) invalid("JSON exceeds 16 MiB limit");
        validate_utf8(text);
        if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
        Reader reader{text}; auto result=reader.read(); reader.space();
        if (reader.pos!=text.size()) invalid("Trailing JSON content at byte "+std::to_string(reader.pos));
        return result;
    } catch (const std::exception& e) { return core::fail(core::ErrorCode::invalid_argument,e.what()); }
}
std::string dump_json(const Json& value) { std::string out; dump(out,value); return out+'\n'; }
core::Result<Json> read_json(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(file,ec);
    if (ec || size>max_bytes) return core::fail(core::ErrorCode::failed,"Cannot read bounded JSON: "+path_string(file));
    std::ifstream input(file,std::ios::binary);
    std::string text(static_cast<std::size_t>(size),'\0');
    if (!input.read(text.data(),static_cast<std::streamsize>(size))) return core::fail(core::ErrorCode::failed,"JSON read failed: "+path_string(file));
    return parse_json(text);
}
core::Result<void> write_text(const std::filesystem::path& file,std::string_view text) {
    try {
        if (!file.parent_path().empty()) iiFileProvider::native::createDirectories(file.parent_path());
        iiFileProvider::native::File::write(file, text);
        return {};
    } catch (const std::exception& e) { return core::fail(core::ErrorCode::failed,e.what()); }
}
std::string path_string(const std::filesystem::path& path) {
    const auto utf8=path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()),utf8.size()};
}
std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path{std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size())};
}
} // namespace ii::builder::detail
