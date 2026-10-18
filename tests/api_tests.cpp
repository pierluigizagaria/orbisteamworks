#include "core/backend.hpp"
#include "core/endpoints.hpp"
#include "exception_observer.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace osw::core;
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct State {
    std::atomic<bool> deliver{false}, started{false}, stopped{false}, bad_thread{false}, failed{false};
    std::mutex mutex;
    std::vector<Request> received;
};
class Fixture final : public Backend {
  public:
    explicit Fixture(std::shared_ptr<State> state) : state(std::move(state)) {}
    void start(const Config &config) override {
        owner = std::this_thread::get_id();
        state->started = true;
        local = {true, 42, config.online_id, {}, 0};
        if (config.virtual_endpoints) {
            config.endpoints->initialize(virtual_endpoint(42, config));
            config.endpoints->set_ready();
        }
    }
    Identity identity() const override { return local; }
    void submit(Request request) override {
        check();
        {
            std::lock_guard lock(state->mutex);
            state->received.push_back(request);
        }
        pending.push_back(std::move(request));
    }
    void cancel(std::uint64_t) override {
        check();
    } // Deliberately deliver late replies to test cancellation ownership.
    Update poll() override {
        check();
        Update update;
        if (state->failed) {
            local.signed_in = false;
            update.failed = true;
            return update;
        }
        if (!state->deliver)
            return update;
        for (auto &request : pending) {
            Result result{request.id, OSW_OK, 200, request.metadata, std::move(request.body)};
            update.results.push_back(std::move(result));
        }
        pending.clear();
        return update;
    }
    void stop() override {
        check();
        state->stopped = true;
    }

  private:
    void check() {
        if (std::this_thread::get_id() != owner)
            state->bad_thread = true;
    }
    std::shared_ptr<State> state;
    std::thread::id owner;
    Identity local;
    std::vector<Request> pending;
};
struct Context {
    std::shared_ptr<State> state = std::make_shared<State>();
    osw_context *value = create_context({{}, "PlayerA", "CUSA13795", "1", "normal"},
                                        std::make_unique<Fixture>(state));
    Context() { require(osw_start(value) == OSW_OK, "start"); }
    ~Context() { osw_destroy(value); }
};
template <class F> void wait(F predicate) {
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < deadline, "wait exceeded contract deadline");
        std::this_thread::sleep_for(2ms);
    }
}
std::uint64_t submit(Context &context, std::string &metadata, std::vector<std::uint8_t> &bytes,
                     int timeout = 1000) {
    osw_request request{sizeof(osw_request), OSW_WEBAPI,      static_cast<std::uint32_t>(timeout),
                        metadata.data(),     metadata.size(), bytes.data(),
                        bytes.size()};
    std::uint64_t id = 0;
    require(osw_submit(context.value, &request, &id) == OSW_OK && id, "submit");
    return id;
}
osw_result result(Context &context, std::uint64_t id) {
    osw_result result{};
    result.size = sizeof(result);
    wait([&] { return osw_get_result(context.value, id, &result) == OSW_OK; });
    return result;
}
void copied_inputs_and_retained_outputs() {
    Context context;
    std::string metadata = "original";
    std::vector<std::uint8_t> bytes{1, 2, 3};
    auto id = submit(context, metadata, bytes);
    metadata = "changed";
    bytes.assign({9});
    context.state->deliver = true;
    auto output = result(context, id);
    require(output.status == OSW_OK && output.body_size == 3 && output.body[0] == 1,
            "request input was not copied");
    require(std::string(output.metadata, output.metadata_size) == "original",
            "metadata was not copied");
    auto pointer = output.body;
    auto other = submit(context, metadata, bytes);
    result(context, other);
    require(pointer[1] == 2, "later request invalidated retained result");
    require(osw_stop(context.value) == OSW_OK && pointer[2] == 3,
            "stop invalidated retained result");
    require(context.state->stopped && !context.state->bad_thread,
            "backend ownership thread violated");
    require(osw_release(context.value, id) == OSW_OK, "release failed");
    output.size = sizeof(output);
    require(osw_get_result(context.value, id, &output) == OSW_NOT_FOUND,
            "released response remained exposed");
}
void cancellation_timeout_and_teardown() {
    Context context;
    std::string metadata = "{}";
    std::vector<std::uint8_t> bytes{1};
    auto cancelled = submit(context, metadata, bytes);
    require(osw_cancel(context.value, cancelled) == OSW_OK, "cancel failed");
    require(result(context, cancelled).status == OSW_ABORTED, "cancel did not complete explicitly");
    auto timed = submit(context, metadata, bytes, 10);
    require(result(context, timed).status == OSW_TIMEOUT, "request did not time out");
    context.state->deliver = true;
    auto barrier = submit(context, metadata, bytes);
    result(context, barrier);
    require(result(context, cancelled).status == OSW_ABORTED &&
                result(context, timed).status == OSW_TIMEOUT,
            "late reply overwrote terminal cancellation/timeout");
    context.state->deliver = false;
    auto stopped = submit(context, metadata, bytes);
    require(osw_stop(context.value) == OSW_OK, "stop failed");
    require(result(context, stopped).status == OSW_ABORTED, "teardown stranded a request");
    std::uint64_t id = 0;
    osw_request request{sizeof(request), OSW_TOKEN, 1000, nullptr, 0, nullptr, 0};
    require(osw_submit(context.value, &request, &id) == OSW_SIGNED_OUT,
            "stopped context accepted work");
}
void admission_limits() {
    Context context;
    std::string metadata = "{}";
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 128; ++i)
        ids.push_back(submit(context, metadata, bytes));
    std::uint64_t id = 0;
    osw_request request{sizeof(request), OSW_TOKEN, 1000, nullptr, 0, nullptr, 0};
    require(osw_submit(context.value, &request, &id) == OSW_LIMIT, "request bound missing");
    require(osw_release(context.value, ids.front()) == OSW_PENDING,
            "pending result released without cancellation");
    osw_cancel(context.value, ids.front());
    osw_release(context.value, ids.front());
    require(osw_submit(context.value, &request, &id) == OSW_OK,
            "released capacity was not reusable");
}
void invalid_config_is_quiet() {
    osw_config config{};
    config.size = sizeof(config);
    config.abi_version = OSW_ABI_VERSION;
    std::strcpy(config.title, "5760");
    std::strcpy(config.build, "1.21");
    std::strcpy(config.realm, "normal");
    std::strcpy(config.online_id, "bad\x01" "id");
    osw_context *context = nullptr;
    ExceptionObserver exceptions;
    require(osw_create(&config, &context) == OSW_INVALID_ARGUMENT && !context,
            "invalid online ID must reject context creation");
    require(exceptions.quiet(), "invalid config raised a host first-chance exception");
}
void backend_failure_completes_pending() {
    Context context;
    std::string metadata = "{}";
    std::vector<std::uint8_t> bytes;
    const auto first = submit(context, metadata, bytes), second = submit(context, metadata, bytes);
    context.state->failed = true;
    require(result(context, first).status == OSW_ABORTED &&
                result(context, second).status == OSW_ABORTED,
            "backend failure must complete all pending operations");
    require(context.state->stopped && !context.state->bad_thread,
            "backend failure must stop on the owning thread");
}
void endpoint_c_api() {
    Config config{"", "Player", "5760", "1.21", "fixture"};
    config.p2p = true;
    config.endpoints = std::make_shared<EndpointRoutes>();
    auto state = std::make_shared<State>();
    auto context = create_context(config, std::make_unique<Fixture>(state));
    osw_endpoint out{sizeof(out)}, input{sizeof(input)};
    require(osw_get_local_endpoint(context, &out) == OSW_NOT_SUPPORTED, "endpoints opt in");
    require(osw_enable_virtual_endpoints(context) == OSW_OK &&
                osw_get_local_endpoint(context, &out) == OSW_PENDING, "enable before startup");
    require(osw_enable_loopback_endpoints(context) == OSW_INVALID_ARGUMENT,
            "selected virtual mode is immutable before start");
    require(osw_set_app_id(context, 0) == OSW_INVALID_ARGUMENT &&
                osw_set_app_id(context, 1234) == OSW_OK, "AppID validation before start");
    require(osw_start(context) == OSW_OK, "endpoint context start");
    wait([&] { return osw_get_local_endpoint(context, &out) == OSW_OK; });
    require(osw_enable_virtual_endpoints(context) == OSW_INVALID_ARGUMENT, "no mode change after start");
    require(osw_enable_loopback_endpoints(context) == OSW_INVALID_ARGUMENT &&
                osw_set_app_id(context, 480) == OSW_INVALID_ARGUMENT, "options fixed after start");
    Endpoint physical{{127, 0, 0, 1}, 32000}, remote = virtual_endpoint(43, config);
    require(config.endpoints->install(43, remote, physical), "publish fixture route");
    copy_endpoint(remote, input);
    require(osw_route_endpoint(context, OSW_ROUTE_OUTBOUND, &input, &input) == OSW_OK &&
                std::string(input.address) == "127.0.0.1" && input.port == 32000,
            "aliased in/out copied correctly");
    require(osw_route_endpoint(context, OSW_ROUTE_INBOUND, &input, &out) == OSW_OK &&
                out.port == remote.port, "inverse C route");
    const auto saved = out;
    config.endpoints->erase(43);
    require(osw_route_endpoint(context, OSW_ROUTE_INBOUND, &input, &out) == OSW_NOT_FOUND &&
                std::memcmp(&saved, &out, sizeof(out)) == 0, "failure preserves caller output");
    require(osw_route_endpoint(context, 0, &input, &out) == OSW_INVALID_ARGUMENT,
            "unknown direction rejected");
    input.port = 0;
    require(osw_route_endpoint(context, OSW_ROUTE_INBOUND, &input, &out) == OSW_INVALID_ARGUMENT,
            "invalid endpoint rejected");
    require(osw_stop(context) == OSW_OK &&
                osw_get_local_endpoint(context, &out) == OSW_UNAVAILABLE, "stop revokes endpoint API");
    osw_destroy(context);
}
} // namespace
int main() {
    try {
        endpoint_c_api();
        invalid_config_is_quiet();
        backend_failure_completes_pending();
        copied_inputs_and_retained_outputs();
        cancellation_timeout_and_teardown();
        admission_limits();
        std::cout << "C API ownership, cancellation, deadlines, teardown and capacity passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
