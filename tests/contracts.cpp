#include <iiSDKBuilder/Builder.h>
#include "Json.h"
#include "BinaryFixtures.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ii::builder;
namespace fs = std::filesystem;
using ii::builder::detail::Json;
using ii::builder::detail::parse_json;
using ii::builder::detail::dump_json;
using ii::builder::detail::read_json;
using ii::builder::detail::path_string;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> void good(const T& result, const std::string& message) {
    if (!result) throw std::runtime_error(message + ": " + result.error().message);
}
bool contains(const Command& command, const std::string& argument) {
    return std::ranges::find(command.arguments, argument) != command.arguments.end();
}
Request request(const fs::path& source, const fs::path& core, const fs::path& output,
                const fs::path& cmake) {
    Request r;
    r.app = {"Hello", source / "tests/fixtures/app", {}, {}, {"hello"}};
    r.sdk_root = core.parent_path();
    r.sdks = {{"Addon", source / "tests/fixtures/addon", {"iiSDKCore"}, {}, {}}};
    r.build_root = output;
    r.cmake = cmake;
    auto host = host_platform();
    good(host, "desktop host");
    r.kits = {{.id="host", .platform=*host}};
    return r;
}
void json_test() {
    auto parsed = parse_json(R"({"escaped":"\"\\\n\uD83D\uDE00","n":12,"b":false,"a":[null,1.5]})");
    good(parsed, "JSON parse");
    require(parsed->at("n").integer() == 12, "integer value");
    require(parsed->at("escaped").string() == "\"\\\n\xF0\x9F\x98\x80", "surrogate decoding");
    auto roundtrip = parse_json(dump_json(*parsed));
    good(roundtrip, "JSON roundtrip");
    require(roundtrip->at("escaped").string() == parsed->at("escaped").string(), "escape roundtrip");
    for (const auto* invalid : {"{\"x\":1,\"x\":2}", "[1,]", "01", "1e", "NaN", "{} garbage",
                               "\"\\uD800\"", "\"\\uDC00\"", "\"\\u0000\"", "\"bad\nline\""})
        require(!parse_json(invalid), std::string("reject malformed JSON: ") + invalid);
    require(!parse_json(std::string(80, '[') + std::string(80, ']')), "bound JSON nesting");
    require(!parse_json("\"\xC0\x80\""), "overlong UTF-8 rejected");
    good(parse_json("\xEF\xBB\xBF{}"), "UTF-8 BOM accepted");
}
void manifest_test(Request r, const fs::path& output) {
    r.app.cache["GREETING"] = "quotes \" and spaces; preserved";
    r.kits.front().environment["BUILDER_EXAMPLE"] = "one=two";
    const auto file = output / "manifest.json";
    good(write_manifest(r, file), "write manifest");
    auto restored = read_manifest(file);
    good(restored, "read manifest");
    require(restored->app.cache == r.app.cache, "cache roundtrip");
    require(restored->kits.front().environment == r.kits.front().environment, "environment roundtrip");
    std::ofstream(file) << R"({"schema":1,"app":{"source":"relative"},"sdkRoot":"../core","typo":true})";
    require(!read_manifest(file), "unknown keys rejected");
    std::ofstream(file) << R"({"schema":1,"app":{"name":"test","source":"relative"},"sdkRoot":"../sdk","jobs":1.5})";
    require(!read_manifest(file), "fractional jobs rejected");
    std::ofstream(file) << R"({"schema":1,"app":{"name":"test","source":"relative"},"sdkRoot":"../sdk","kits":[]})";
    auto relative = read_manifest(file);
    good(relative, "relative paths parsed");
    require(relative->app.source == (output / "relative").lexically_normal(), "relative to manifest");
}
void publication_test(const fs::path& output) {
    fs::create_directories(output);
    const auto target = output / "provider-output.json";
    good(ii::builder::detail::write_text(target, R"({"version":1})"), "provider-owned initial publication");
    good(ii::builder::detail::write_text(target, R"({"version":2})"), "provider-owned atomic replacement");
    auto saved = read_json(target);
    good(saved, "published output remains valid JSON");
    require(saved->at("version").integer() == 2, "replacement is complete");
    require(!ii::builder::detail::write_text(target / "child.json", "incomplete"), "invalid parent is an explicit publication failure");
    saved = read_json(target);
    good(saved, "publication failure preserves existing data");
    require(saved->at("version").integer() == 2, "failed publication never truncates the original");
}
void graph_test(Request r) {
    r.sdks.push_back({"Extra", r.sdks.front().source, {"Addon"}, {}, {}});
    std::reverse(r.sdks.begin(), r.sdks.end());
    auto plan = make_plan(r);
    good(plan, "graph planning");
    const auto& p = plan->kits.front().projects;
    require(p.size() == 4 && p[0].project.name == "iiSDKCore" &&
        p[1].project.name == "Addon" && p[2].project.name == "Extra" &&
        p[3].project.name == "Hello", "topological SDK order, application last");
    r.sdks[1].depends = {"Extra"};
    require(!make_plan(r), "cycle rejected");
    r.sdks[1].depends = {"Missing"};
    require(!make_plan(r), "unknown SDK rejected");
    r.sdks[1].depends.clear();
    r.sdks[0].name = "../escape";
    require(!make_plan(r), "unsafe SDK directory rejected");
    r.sdks[0].name = "addon";
    require(!make_plan(r), "case insensitive SDK collisions rejected");
}
void kits_test(Request r) {
    r.kits = default_kits();
    auto plan = make_plan(r);
    good(plan, "six-platform plan");
    require(plan->kits.size() == 6, "all six platforms represented");
    auto host = host_platform();
    for (const auto& k : plan->kits) {
        if (k.kit.platform == *host) require(k.unavailable_reason.empty(), "native kit available");
        if (k.kit.platform != *host &&
            (k.kit.platform == Platform::windows || k.kit.platform == Platform::linux_os))
            require(!k.unavailable_reason.empty(), "foreign desktop needs toolchain");
    }
    r.kits.front().toolchain = r.build_root / "does-not-exist.cmake";
    plan = make_plan(r);
    good(plan, "unavailable kit still reportable");
    require(!plan->kits.front().unavailable_reason.empty(), "missing toolchain unavailable");
    r.kits[1].id = r.kits[0].id;
    require(!make_plan(r), "duplicate kit rejected");
    r.kits[1].id="CON";
    require(!make_plan(r), "Windows reserved kit name rejected on every host");
}
void commands_test(Request r) {
    r.app.cache["GREETING"] = "space ; ' $(echo bad) \" \\";
    r.kits.front().environment["BUILD_COLOR"] = "two words";
    auto plan = make_plan(r);
    good(plan, "command planning");
    const auto& k = plan->kits.front();
    require(k.projects[0].commands.size() == 3, "SDK configure, build, install");
    require(k.projects.back().commands.size() == 2, "app configure, build");
    const auto& configure = k.projects.back().commands.front();
    require(contains(configure, "-DGREETING=" + r.app.cache["GREETING"]), "literal cache argument");
    require(contains(configure, "BUILD_COLOR=two words"), "per-child environment");
    require(contains(configure, "-DCMAKE_PREFIX_PATH=" + path_string(k.prefix)), "target prefix only");
    require(contains(k.projects.back().commands.back(), "hello"), "app target selected");
    require(!fs::exists(r.build_root), "planning has no filesystem mutation");
    r.app.cache["CMAKE_TOOLCHAIN_FILE"] = "/wrong";
    require(!make_plan(r), "project cannot replace kit toolchain");
    r.app.cache.erase("CMAKE_TOOLCHAIN_FILE");
    r.build_root = r.app.source;
    require(!make_plan(r), "source output refused");
    r.build_root = r.app.source.parent_path() / "out";
    require(!make_plan(r), "outputs belong under build");
    r.build_root = r.app.source / "build";
    r.kits.front().platform = Platform::android;
    r.kits.front().android_abi = "invalid-abi";
    require(!make_plan(r), "Android ABI validated");
}
void failure_test(Request r) {
    r.kits.push_back(r.kits.front());
    r.kits.back().id = "second";
    Builder b{[](const Command& c, std::stop_token) -> ii::core::Result<ProcessOutcome> {
        const bool first = path_string(c.log).find("/host/") != std::string::npos;
        if ((first && c.project == "iiSDKCore" && c.phase == Phase::build) ||
            (!first && c.project == "Hello" && c.phase == Phase::build))
            return ProcessOutcome{17};
        if (c.phase == Phase::configure) {
            const auto where=std::ranges::find(c.arguments,"-B");
            const fs::path binary=*(where+1);
            const auto host=host_platform();
            const auto system=*host==Platform::macos?"Darwin":(*host==Platform::windows?"Windows":"Linux");
            std::ofstream(binary/"iisdkbuilder-system.txt") << system << '\n';
        }
        return ProcessOutcome{0};
    }};
    auto report = b.build(r);
    good(report, "partial build report");
    require(!report->succeeded && report->kits.size() == 2, "partial matrix fails");
    require(report->kits[0].status == Status::failed && report->kits[1].status == Status::failed,
        "independent kits continue on failure");
    const auto& steps = report->kits[0].steps;
    require(std::ranges::any_of(steps, [](const auto& s) {
        return s.command.project == "Hello" && s.status == Status::blocked;
    }), "dependent app blocked after SDK failure");
    auto json = read_json(report->file);
    good(json, "persisted failure report");
    require(json->at("kits").array()[0].at("steps").array()[1].at("exitCode").integer() == 17,
        "child exit code preserved");
    fs::create_directories(r.build_root / ".iisdkbuilder.lock");
    require(!b.build(r), "concurrent root locked");
    fs::remove(r.build_root / ".iisdkbuilder.lock");
}
void cancellation_test(Request r) {
    std::stop_source stop;
    stop.request_stop();
    std::atomic<unsigned> calls{0};
    Builder b{[&](const Command&, std::stop_token) -> ii::core::Result<ProcessOutcome> {
        ++calls;
        return ProcessOutcome{0};
    }};
    auto report = b.build(r, stop.get_token());
    good(report, "cancelled report");
    require(!report->succeeded && calls == 0 && report->kits[0].status == Status::cancelled,
        "pre-cancellation starts no child");
}
void file_api_test(Request r) {
    auto fake=[&](const Command& c,std::stop_token) -> ii::core::Result<ProcessOutcome> {
        if (c.phase!=Phase::configure) return ProcessOutcome{0};
        const auto where=std::ranges::find(c.arguments,"-B");
        const fs::path binary=*(where+1);
        const auto host=host_platform();
        std::ofstream(binary/"iisdkbuilder-system.txt") <<
            (*host==Platform::macos?"Darwin":(*host==Platform::windows?"Windows":"Linux")) << '\n';
        if (c.project=="Hello") {
            const auto replies=binary/".cmake/api/v1/reply";
            fs::create_directories(replies);
            std::ofstream(replies/"index-fixture.json") <<
                R"({"reply":{"client-iiSDKBuilder":{"codemodel-v2":{"jsonFile":"model.json"}}}})";
            std::ofstream(replies/"model.json") <<
                R"({"configurations":[{"name":"Release","targets":[{"name":"hello","jsonFile":"target.json"}]}]})";
            std::ofstream(replies/"target.json") <<
                R"({"type":"EXECUTABLE","nameOnDisk":"hello","artifacts":[{"path":"Release${EFFECTIVE_PLATFORM_NAME}/hello"},{"path":"Release${EFFECTIVE_PLATFORM_NAME}/hello.pdb"}]})";
            fs::create_directories(binary/"Release");
            fixture::host_image(binary/"Release/hello");
            std::ofstream(binary/"Release/hello.pdb") << "auxiliary debug data\n";
        }
        return ProcessOutcome{0};
    };
    auto report=Builder{fake}.build(r);
    good(report,"File API fixture report");
    require(report->succeeded && report->kits[0].artifacts.size()==1,"File API references and Xcode placeholder resolved");
    require(report->kits[0].verified_artifacts.size()==1 &&
        report->kits[0].verified_artifacts[0].execution==ExecutionModel::native,"successful native artifact inspected");
    auto script=Builder{[&](const Command& c,std::stop_token stop) -> ii::core::Result<ProcessOutcome> {
        auto result=fake(c,stop);
        if (c.project=="Hello" && c.phase==Phase::build) {
            auto plan=make_plan(r);
            std::ofstream(plan->kits[0].projects.back().binary_dir/"Release/hello") << "#!/usr/bin/env node\nconsole.log('fake native app');\n";
        }
        return result;
    }}.build(r);
    good(script,"interpreter artifact report");
    require(!script->succeeded && script->kits[0].status==Status::failed && script->kits[0].verified_artifacts.empty(),
        "successful CMake exit cannot certify an interpreter launcher as native");
    r.kits.front().generator="Ninja Multi-Config";
    auto changed=Builder{fake}.build(r);
    good(changed,"changed identity report");
    require(!changed->succeeded && changed->kits[0].message.find("identity changed")!=std::string::npos,
        "incompatible kit cannot reuse cached outputs");
    r.kits.front().generator.clear();
    auto missing=Builder{[&](const Command& c,std::stop_token stop) -> ii::core::Result<ProcessOutcome> {
        auto result=fake(c,stop);
        if (c.project=="Hello" && c.phase==Phase::build) {
            const auto plan=make_plan(r);
            fs::remove(plan->kits[0].projects.back().binary_dir/"Release/hello");
        }
        return result;
    }}.build(r);
    good(missing,"missing artifact report");
    require(!missing->succeeded && missing->kits[0].steps.size()==8,"missing artifact fails while preserving completed command history");
    auto unverified=Builder{[](const Command&,std::stop_token) -> ii::core::Result<ProcessOutcome> {
        return ProcessOutcome{0};
    }}.build(r);
    good(unverified,"missing marker report");
    require(!unverified->succeeded && unverified->kits[0].steps[0].status==Status::failed,
        "successful process exit cannot bypass system verification");
}
void process_test(const fs::path& output, const fs::path& cmake) {
    fs::create_directories(output);
    const std::string literal = "quotes \" ' ; $(touch SHOULD_NOT_EXIST) & trailing\\";
    Command c{Phase::build, "process", {path_string(cmake), "-E", "echo", literal}, output / "literal.log"};
    auto outcome = run_process(c);
    good(outcome, "spawn literal command");
    require(outcome->exit_code == 0, "echo succeeds");
    std::ifstream input(c.log);
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    require(line == literal, "shell metacharacters and trailing slashes preserved");
    c.arguments={path_string(cmake),"-E","false"};
    c.log=output/"nonzero.log";
    outcome=run_process(c);
    good(outcome,"nonzero native child result");
    require(outcome->exit_code==1 && !outcome->cancelled && !outcome->timed_out,"native child failure remains an exit outcome");
    c.arguments = {path_string(cmake), "-E", "sleep", "5"};
    c.log = output / "timeout.log";
    c.timeout = std::chrono::seconds{1};
    outcome = run_process(c);
    good(outcome, "timeout result");
    require(outcome->timed_out && outcome->exit_code != 0, "timeout terminates child");
    c.timeout = std::chrono::seconds{20};
    c.log = output / "cancel.log";
    std::stop_source stop;
    auto running = std::async(std::launch::async, [&] { return run_process(c, stop.get_token()); });
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    stop.request_stop();
    outcome = running.get();
    good(outcome, "cancellation result");
    require(outcome->cancelled, "running child cancelled");
    c.arguments.front() = path_string(output / "missing-executable");
    require(!run_process(c), "spawn error distinct from child exit");
    const auto script=output/"large-output.cmake";
    good(ii::builder::detail::write_text(script,
        "string(REPEAT \"binary-safe-build-output; \\\" 끝\" 65536 payload)\nmessage(\"${payload}\")\n"),"provider creates output fixture");
    c.arguments={path_string(cmake),"-P",path_string(script)};
    c.log=output/"large.log";
    outcome=run_process(c);
    good(outcome,"large output streamed through common Core process API");
    require(outcome->exit_code==0 && fs::file_size(c.log)>1024*1024,"large log completely drained without capture growth");
    c.log=output;
    require(!run_process(c),"log creation failure cannot report build success");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 6, "test arguments");
        const std::string which = argv[1];
        const fs::path source = argv[2], core = argv[3], output = argv[4], cmake = argv[5];
        if (fs::exists(output)) fs::remove_all(output);
        auto r = request(source, core, output, cmake);
        if (which == "json") { json_test(); publication_test(output); }
        else if (which == "manifest") manifest_test(r, output);
        else if (which == "graph") graph_test(r);
        else if (which == "kits") kits_test(r);
        else if (which == "commands") commands_test(r);
        else if (which == "failure") failure_test(r);
        else if (which == "cancellation") cancellation_test(r);
        else if (which == "file_api") file_api_test(r);
        else if (which == "process") process_test(output, cmake);
        else throw std::runtime_error("unknown test case");
        std::cout << which << ": passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
