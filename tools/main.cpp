#include <iiSDKBuilder/Builder.h>
#include <iiSDKBuilder/Resources.h>
#include <iiSDKBuilder/Hls.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using namespace ii::builder;
namespace fs=std::filesystem;
namespace {
std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void interrupt(int) { interrupted.store(true,std::memory_order_relaxed); }
fs::path path(std::string_view input) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(input.data()),input.size()));
}
unsigned integer(const std::string& value) {
    unsigned result=0;
    const auto converted=std::from_chars(value.data(),value.data()+value.size(),result);
    if (converted.ec!=std::errc{} || converted.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid integer: "+value);
    return result;
}
void help() {
    std::cout << "iiSDKBuilder " IISDKBUILDER_VERSION " - Qt-free CMake SDK/application matrix builder\n"
        "Usage: iisdk-builder <targets|init|doctor|plan|build|resources|hls> [options]\n"
        "  hls --source CPP --entry FUNCTION --output DIR [--compiler CLANG] [--interface ready-valid|combinational]\n"
        "  targets              List commercial/embedded profiles as JSON\n"
        "  resources --manifest FILE --platform TARGET --cpp FILE --header FILE [--pack FILE] [--depfile FILE]\n"
        "  resources --manifest FILE --platform TARGET --pack FILE\n"
        "  --manifest FILE       Read/write manifest (default iiSDKBuilder.json)\n"
        "  --app DIRECTORY       App CMake source (required without a manifest)\n"
        "  --sdk-root DIRECTORY  iiSDK sources, including iiSDKCore\n"
        "  --platform LIST       windows,macos,linux,android,ios,wasm,automotive,arduino,fpga or host\n"
        "  --build-root DIR      Output under build/ (default APP/build/iiSDKBuilder)\n"
        "  --configuration NAME  CMake configuration (default Release)\n"
        "  --jobs N              Compiler jobs per kit (default 2)\n"
        "  --parallel-kits N     Concurrent kits (default 2)\n"
        "  --timeout N           Per-command timeout in seconds (default 1800)\n"
        "  --deploy             Program/upload using the explicit kit contract\n"
        "  --run                Execute/monitor using the explicit kit runner\n"
        "  --cmake EXECUTABLE    CMake program\n"
        "Five OS targets require native machine-code images; WASM is a separate web target.\n"
        "All six platforms are requested by default. Missing kits fail the matrix.\n";
}
int hls_command(int argc,char** argv) {
    HlsRequest request;
    std::set<std::string> seen;
    for(int i=2;i<argc;++i) {
        const std::string key=argv[i];
        if(key=="--help"){help();return 0;}
        if(++i==argc)throw std::invalid_argument("Missing HLS option value");
        const std::string value=argv[i];
        if(key!="--include"&&key!="--define"&&!seen.insert(key).second)throw std::invalid_argument("Repeated HLS option: "+key);
        if(key=="--source")request.source=path(value);
        else if(key=="--entry")request.entry=value;
        else if(key=="--module")request.module=value;
        else if(key=="--output")request.output_directory=path(value);
        else if(key=="--compiler")request.compiler=path(value);
        else if(key=="--include")request.includes.push_back(path(value));
        else if(key=="--define"){const auto at=value.find('=');request.definitions[value.substr(0,at)]=at==value.npos?"1":value.substr(at+1);}
        else if(key=="--max-unroll")request.max_unroll=integer(value);
        else if(key=="--max-nodes")request.max_nodes=integer(value);
        else if(key=="--clock-period-ps")request.clock_period_ps=integer(value);
        else if(key=="--timeout")request.timeout=std::chrono::seconds(integer(value));
        else if(key=="--interface"){if(value=="ready-valid")request.interface=HlsInterface::ready_valid;else if(value=="combinational")request.interface=HlsInterface::combinational;else throw std::invalid_argument("Unknown HLS interface");}
        else throw std::invalid_argument("Unknown HLS option: "+key);
    }
    if(request.source.empty()||request.entry.empty())throw std::invalid_argument("hls requires --source and --entry");
    if(request.output_directory.empty())request.output_directory=request.source.parent_path()/"build/hls";
    auto result=synthesize_hls(request);if(!result)throw std::runtime_error(result.error().message);
    std::cout<<hls_report_json(*result);return 0;
}
int resource_command(int argc,char** argv) {
    ResourceOutputs outputs;fs::path manifest;
    auto host=host_platform();if (!host) throw std::invalid_argument(host.error().message);
    auto target=*host;
    std::set<std::string> seen;
    for (int i=2;i<argc;++i) {
        const std::string key=argv[i];
        if (key=="--help") { help();return 0; }
        if (++i==argc || !seen.insert(key).second) throw std::invalid_argument("Missing/repeated resource option: "+key);
        if (key=="--manifest") manifest=path(argv[i]);
        else if (key=="--cpp") outputs.cpp=path(argv[i]);
        else if (key=="--header") outputs.header=path(argv[i]);
        else if (key=="--pack") outputs.pack=path(argv[i]);
        else if (key=="--depfile") outputs.depfile=path(argv[i]);
        else if (key=="--platform") {
            if (std::string_view(argv[i])!="host") {
                auto platform=parse_platform(argv[i]);if (!platform) throw std::invalid_argument(platform.error().message);
                target=*platform;
            }
        } else throw std::invalid_argument("Unknown resource option: "+key);
    }
    if (manifest.empty()) throw std::invalid_argument("resources requires --manifest");
    auto report=compile_resources(manifest,target,outputs);
    if (!report) throw std::runtime_error(report.error().message);
    std::cout<<"RESOURCE_BUILD_OK name="<<report->name<<" platform="<<platform_name(target)<<" entries="<<report->entries
        <<" unique_payloads="<<report->unique_payloads<<" logical_bytes="<<report->logical_bytes
        <<" stored_bytes="<<report->stored_bytes<<" changed="<<std::boolalpha<<report->changed<<'\n';
    return 0;
}
}
int run_cli(int argc,char** argv) {
    try {
        if (argc<2 || std::string_view(argv[1])=="--help" || std::string_view(argv[1])=="-h") { help();return 0; }
        const std::string action=argv[1];
        if(action=="targets"){if(argc!=2)throw std::invalid_argument("targets takes no options");std::cout<<target_profiles_json();return 0;}
        if (action=="resources") return resource_command(argc,argv);
        if (action=="hls") return hls_command(argc,argv);
        if (action=="--version") { std::cout << "iiSDKBuilder " IISDKBUILDER_VERSION "\n";return 0; }
        if (action!="init" && action!="doctor" && action!="plan" && action!="build") throw std::invalid_argument("Unknown command: "+action);
        std::map<std::string,std::string> options;
        for (int i=2;i<argc;++i) {
            const std::string key=argv[i];
            if (key=="--help") { help();return 0; }
            if(key=="--deploy"||key=="--run"){if(!options.emplace(key,"1").second)throw std::invalid_argument("Repeated option: "+key);continue;}
            if (key!="--manifest" && key!="--app" && key!="--sdk-root" && key!="--platform" &&
                key!="--build-root" && key!="--configuration" && key!="--jobs" &&
                key!="--parallel-kits" && key!="--timeout" && key!="--cmake") throw std::invalid_argument("Unknown option: "+key);
            if (++i==argc) throw std::invalid_argument("Missing value for "+key);
            if (!options.emplace(key,argv[i]).second) throw std::invalid_argument("Repeated option: "+key);
        }
        const auto manifest=options.contains("--manifest")?path(options.at("--manifest")):fs::path{"iiSDKBuilder.json"};
        Request r;
        if (action!="init" && (options.contains("--manifest") || (!options.contains("--app") && fs::exists(manifest)))) {
            auto read=read_manifest(manifest);
            if (!read) throw std::runtime_error(read.error().message);
            r=std::move(*read);
        }
        if(options.contains("--deploy"))r.deploy=true;
        if(options.contains("--run"))r.run=true;
        if (options.contains("--app")) {
            r.app.source=fs::absolute(path(options.at("--app"))).lexically_normal();
            r.app.name=r.app.source.filename().string();
            // Directory names may contain spaces; the project label is independent.
            if (r.app.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos) r.app.name="App";
        }
        if (options.contains("--sdk-root")) r.sdk_root=fs::absolute(path(options.at("--sdk-root"))).lexically_normal();
        if (options.contains("--build-root")) r.build_root=fs::absolute(path(options.at("--build-root"))).lexically_normal();
        if (options.contains("--cmake")) r.cmake=path(options.at("--cmake"));
        if (options.contains("--configuration")) r.configuration=options.at("--configuration");
        if (options.contains("--jobs")) r.jobs=integer(options.at("--jobs"));
        if (options.contains("--parallel-kits")) r.parallel_kits=integer(options.at("--parallel-kits"));
        if (options.contains("--timeout")) r.timeout=std::chrono::seconds{integer(options.at("--timeout"))};
        if (options.contains("--platform")) {
            std::vector<Platform> selected;
            std::string_view list=options.at("--platform");
            while (!list.empty()) {
                const auto comma=list.find(',');
                const auto name=list.substr(0,comma);
                auto platform=name=="host"?host_platform():parse_platform(name);
                if (!platform) throw std::invalid_argument(platform.error().message);
                selected.push_back(*platform);
                if (comma==std::string_view::npos) break;
                list.remove_prefix(comma+1);
                if (list.empty()) throw std::invalid_argument("Empty platform selection");
            }
            std::erase_if(r.kits,[&](const auto& k) { return std::ranges::find(selected,k.platform)==selected.end(); });
            for (const auto p:selected) if (std::ranges::none_of(r.kits,[&](const auto& k) { return k.platform==p; }))
                throw std::invalid_argument("Manifest has no kit for selected platform: "+std::string(platform_name(p)));
        }
        auto plan=make_plan(r);
        if (!plan) throw std::invalid_argument(plan.error().message);
        if (action=="init") {
            if (fs::exists(manifest)) throw std::invalid_argument("Manifest already exists: "+manifest.string());
            // Keep automatic kit discovery for later runs; do not freeze this host's paths.
            r.app=plan->request.app;r.sdk_root=plan->request.sdk_root;r.build_root=plan->request.build_root;
            auto saved=write_manifest(r,manifest);
            if (!saved) throw std::runtime_error(saved.error().message);
            std::cout << fs::absolute(manifest).string() << '\n';return 0;
        }
        if (action=="plan") { std::cout << plan_json(*plan);return 0; }
        if (action=="doctor") {
            bool available=true;
            for (const auto& kit:plan->kits) {
                std::cout << kit.kit.id << " [" << execution_model_name(execution_model(kit.kit)) << "]: " <<
                    (kit.unavailable_reason.empty()?"ready (preflight only)":kit.unavailable_reason) << '\n';
                available=available && kit.unavailable_reason.empty();
            }
            return available?0:1;
        }
        std::mutex output;
        Builder builder{[&](const Command& command,std::stop_token token) {
            { std::lock_guard guard(output);std::cerr << command.project << ' ' << phase_name(command.phase) << " -> " << command.log.string() << '\n'; }
            return run_process(command,token);
        }};
        std::stop_source stop;
        const auto old_int=std::signal(SIGINT,interrupt),old_term=std::signal(SIGTERM,interrupt);
        std::jthread monitor([&](std::stop_token token) {
            while (!token.stop_requested()) {
                if (interrupted.load(std::memory_order_relaxed)) { stop.request_stop();return; }
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
        });
        auto report=builder.build(r,stop.get_token());
        monitor.request_stop();monitor.join();
        std::signal(SIGINT,old_int);std::signal(SIGTERM,old_term);
        if (!report) throw std::runtime_error(report.error().message);
        std::cout << report_json(*report);
        if (stop.stop_requested()) return 130;
        return report->succeeded?0:1;
    } catch (const std::exception& e) { std::cerr << "iiSDKBuilder: " << e.what() << '\n';return 2; }
}
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv) {
    try {
        std::vector<std::string> storage;storage.reserve(static_cast<std::size_t>(argc));
        for (int i=0;i<argc;++i) {
            const auto utf8=fs::path(argv[i]).u8string();
            storage.emplace_back(reinterpret_cast<const char*>(utf8.data()),utf8.size());
        }
        std::vector<char*> arguments;arguments.reserve(storage.size()+1);
        for (auto& value:storage) arguments.push_back(value.data());
        arguments.push_back(nullptr);return run_cli(argc,arguments.data());
    } catch (const std::exception& error) { std::cerr<<"iiSDKBuilder native arguments: "<<error.what()<<'\n';return 2; }
}
#else
int main(int argc,char** argv) { return run_cli(argc,argv); }
#endif
