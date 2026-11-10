#include <iiSDKCore/Scripting/CPythonHost.h>

#include <future>
#include <iostream>
#include <stdexcept>

using namespace ii;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv) {
    try {
        check(argc == 2, "compiled module directory required");
        runtime::Runtime runtime({.workers=2, .capacity=32});
        runtime::SerialQueue queue(runtime);
        auto host = scripting::CPythonHost::attach(queue, {.module_paths={argv[1]}});
        check(bool(host), "attach compiled Python modules");
        auto invoke = [&](scripting::CPythonHost& target) -> core::Result<core::Value> {
            std::promise<core::Result<core::Value>> promise;
            auto future = promise.get_future();
            auto submitted = target.queries().post({"iixml_aot_probe", "verify", {}}, [&](auto reply) {
                if (!reply) promise.set_value(core::fail(core::ErrorCode::failed, "query delivery failed"));
                else promise.set_value(std::move(*reply));
            });
            check(bool(submitted), "submit AOT Python call");
            return future.get();
        };
        auto result = invoke(**host);
        if (!result) throw std::runtime_error(result.error().message);
        auto object = result->get<core::Value::Object>();
        check(bool(object), "AOT result is an object");
        check(object->get().at("implementation") == core::Value("cpython"), "CPython implementation");
        if (object->get().at("matrix_compiled") != core::Value(true)) {
            auto origin = object->get().at("matrix_origin").get<std::string>();
            auto loader = object->get().at("matrix_loader").get<std::string>();
            if (origin && loader) std::cerr << "matrix origin=" << origin->get() << " loader=" << loader->get() << '\n';
        }
        check(object->get().at("matrix_compiled") == core::Value(true), "iiXml module is native AOT");
        check(object->get().at("probe_compiled") == core::Value(true), "entry module is native AOT");
        check(object->get().at("dynamic") == core::Value(std::int64_t{12}), "dynamic Python execution");
        check(object->get().at("values") == core::Value(core::Value::Array{
            core::Value(std::int64_t{30}), core::Value(std::int64_t{70}),
            core::Value(std::int64_t{70}), core::Value(std::int64_t{174})}), "iiXml tensor result");
        check(object->get().at("means") == core::Value(core::Value::Array{
            core::Value(3.5), core::Value(5.5)}), "iiXml axis means");
        check(object->get().at("batches") == core::Value(core::Value::Array{
            core::Value(std::int64_t{7}), core::Value(std::int64_t{10}),
            core::Value(std::int64_t{15}), core::Value(std::int64_t{22}),
            core::Value(std::int64_t{67}), core::Value(std::int64_t{78}),
            core::Value(std::int64_t{91}), core::Value(std::int64_t{106})}), "iiXml batched matrix product");
        check(object->get().at("xml_roundtrip") == object->get().at("values"), "iiXml XML round trip");
        check(object->get().at("invalid_rejected") == core::Value(true), "iiXml rejects nonnumeric XML");
        check(object->get().at("dynamic_import") == core::Value(9.0), "dynamic Python import");
        auto repeated = invoke(**host);
        if (!repeated) {
            auto origin = object->get().at("matrix_origin").get<std::string>();
            auto loader = object->get().at("matrix_loader").get<std::string>();
            if (origin && loader) std::cerr << "matrix origin=" << origin->get() << " loader=" << loader->get() << '\n';
            throw std::runtime_error("Repeated AOT call failed: " + repeated.error().message);
        }
        auto second = scripting::CPythonHost::attach(queue, {.module_paths={argv[1]}});
        check(bool(second), "second Core host attaches");
        auto shared = invoke(**second);
        if (!shared) throw std::runtime_error("Second host AOT call failed: " + shared.error().message);
        second->reset();
        host->reset();
        check(bool(queue.close()), "close runtime queue");
        check(bool(runtime.join()), "join runtime");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
