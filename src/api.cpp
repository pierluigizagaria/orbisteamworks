#include "core/backend.hpp"
#include "core/credential.hpp"
#include "core/endpoints.hpp"
#include "core/local_io.hpp"
#include "lab/backend.hpp"
#include "steam/backend.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t max_requests = 128, max_bytes = 16 * 1024 * 1024;
std::atomic<bool> production_context{false};
template <std::size_t N> bool read_string(const char (&data)[N], std::string &out) {
    auto end = static_cast<const char *>(std::memchr(data, 0, N));
    if (!end)
        return false;
    out.assign(data, end);
    return true;
}
template <std::size_t N> void copy_string(char (&output)[N], const std::string &value) {
    auto length = std::min(value.size(), N - 1);
    std::memcpy(output, value.data(), length);
    output[length] = 0;
}
bool namespace_part(const std::string &value) {
    if (value.empty())
        return false;
    for (char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}
} // namespace

struct osw_context {
    struct Job {
        osw::core::Request request;
        osw::core::Result result;
        Clock::time_point deadline;
        std::size_t input_size = 0;
        bool ready = false;
    };
    osw::core::Config config;
    std::unique_ptr<osw::core::Backend> backend;
    std::mutex mutex, lifecycle;
    std::condition_variable wake;
    std::thread worker;
    std::map<std::uint64_t, std::shared_ptr<Job>> jobs;
    std::deque<std::uint64_t> queued, cancellations;
    std::deque<osw::core::Event> events;
    osw::core::Identity identity;
    std::uint64_t next_id = 1;
    std::size_t inputs = 0, outputs = 0;
    bool started = false, stopping = false, overflow = false, production = false;

    void event(osw::core::Event value) {
        if (events.size() == 512) {
            overflow = true;
            return;
        }
        try {
            events.push_back(std::move(value));
        } catch (...) {
            overflow = true;
        }
    }
    void complete(osw::core::Result result) {
        auto it = jobs.find(result.id);
        if (it == jobs.end() || it->second->ready)
            return;
        auto &job = *it->second;
        auto bytes = result.metadata.size() + result.body.size();
        if (bytes > max_bytes - outputs) {
            result.status = OSW_LIMIT;
            result.metadata.clear();
            result.body.clear();
            bytes = 0;
        }
        inputs -= job.input_size;
        job.input_size = 0;
        job.request.metadata.clear();
        job.request.body.clear();
        outputs += bytes;
        job.result = std::move(result);
        job.ready = true;
        event({OSW_REQUEST_COMPLETE, job.result.id});
    }
    void run() {
        try {
            backend->start(config);
            {
                std::lock_guard lock(mutex);
                identity = backend->identity();
                event({OSW_IDENTITY_CHANGED});
            }
            for (;;) {
                std::deque<std::uint64_t> submit, cancel;
                {
                    std::lock_guard lock(mutex);
                    if (stopping)
                        break;
                    submit.swap(queued);
                    cancel.swap(cancellations);
                }
                for (auto id : cancel)
                    backend->cancel(id);
                for (auto id : submit) {
                    osw::core::Request request;
                    {
                        std::lock_guard lock(mutex);
                        auto it = jobs.find(id);
                        if (it == jobs.end() || it->second->ready)
                            continue;
                        if (Clock::now() >= it->second->deadline) {
                            complete({id, OSW_TIMEOUT});
                            continue;
                        }
                        request = std::move(it->second->request);
                    }
                    backend->submit(std::move(request));
                }
                auto update = backend->poll();
                auto current_identity = backend->identity();
                {
                    std::lock_guard lock(mutex);
                    for (auto &result : update.results) {
                        auto it = jobs.find(result.id);
                        if (it != jobs.end() && !it->second->ready &&
                            Clock::now() >= it->second->deadline) {
                            complete({result.id, OSW_TIMEOUT});
                            cancellations.push_back(result.id);
                        } else
                            complete(std::move(result));
                    }
                    for (auto &value : update.events)
                        event(std::move(value));
                    if (current_identity.signed_in != identity.signed_in ||
                        current_identity.account_id != identity.account_id ||
                        current_identity.online_id != identity.online_id ||
                        current_identity.reason_code != identity.reason_code) {
                        identity = std::move(current_identity);
                        event({OSW_IDENTITY_CHANGED});
                    }
                    for (const auto &[id, job] : jobs)
                        if (!job->ready && Clock::now() >= job->deadline) {
                            complete({id, OSW_TIMEOUT});
                            cancellations.push_back(id);
                        }
                    if (update.failed)
                        stopping = true;
                }
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(20), [&] {
                    return stopping || !queued.empty() || !cancellations.empty();
                });
            }
        } catch (const osw::Error &error) {
            std::lock_guard lock(mutex);
            identity = {
                false, 0, {}, "Platform initialization failed", static_cast<int>(error.code)};
            stopping = true;
        } catch (...) {
            std::lock_guard lock(mutex);
            identity = {false, 0, {}, "Platform backend failed", OSW_UNAVAILABLE};
            stopping = true;
        }
        try {
            backend->stop();
        } catch (...) {
        }
        config.endpoints->close();
        std::lock_guard lock(mutex);
        identity.signed_in = false;
        identity.account_id = 0;
        for (const auto &[id, job] : jobs)
            if (!job->ready)
                complete({id, OSW_ABORTED});
        event({OSW_IDENTITY_CHANGED});
    }
};

namespace osw::core {
osw_context *create_context(Config config, std::unique_ptr<Backend> backend) {
    auto context = std::make_unique<osw_context>();
    if (!config.endpoints)
        config.endpoints = std::make_shared<EndpointRoutes>();
    context->config = std::move(config);
    context->backend = std::move(backend);
    return context.release();
}
} // namespace osw::core

extern "C" {
int32_t osw_enable_virtual_endpoints(osw_context *context) {
    if (!context) return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lifecycle(context->lifecycle);
        std::lock_guard lock(context->mutex);
        if (context->started || context->stopping) return OSW_INVALID_ARGUMENT;
        if (!context->config.p2p) return OSW_NOT_SUPPORTED;
        context->config.virtual_endpoints = true;
        context->config.endpoints->enable();
        return OSW_OK;
    } catch (...) { return OSW_UNAVAILABLE; }
}
int32_t osw_get_local_endpoint(osw_context *context, osw_endpoint *output) {
    if (!context || !output || output->size != sizeof(*output)) return OSW_INVALID_ARGUMENT;
    try {
        osw::core::Endpoint value;
        const auto status = context->config.endpoints->local_endpoint(value);
        if (status == OSW_OK) osw::core::copy_endpoint(value, *output);
        return status;
    } catch (...) { return OSW_UNAVAILABLE; }
}
int32_t osw_route_endpoint(osw_context *context, uint32_t direction,
                         const osw_endpoint *input, osw_endpoint *output) {
    if (!context || !input || !output || output->size != sizeof(*output) ||
        (direction != OSW_ROUTE_OUTBOUND && direction != OSW_ROUTE_INBOUND))
        return OSW_INVALID_ARGUMENT;
    try {
        osw::core::Endpoint from, to;
        if (!osw::core::read_endpoint(*input, from)) return OSW_INVALID_ARGUMENT;
        const auto status = context->config.endpoints->lookup(direction, from, to);
        if (status == OSW_OK) osw::core::copy_endpoint(to, *output);
        return status;
    } catch (...) { return OSW_UNAVAILABLE; }
}
int32_t osw_create(const osw_config *config, osw_context **output) {
    if (!output)
        return OSW_INVALID_ARGUMENT;
    *output = nullptr;
    try {
        if (!config || config->size != sizeof(*config) || config->abi_version != OSW_ABI_VERSION)
            return OSW_INVALID_ARGUMENT;
        osw::core::Config copied;
        if (!read_string(config->library_path, copied.library_path) ||
            !read_string(config->online_id, copied.online_id) ||
            !read_string(config->title, copied.title) ||
            !read_string(config->build, copied.build) ||
            !read_string(config->realm, copied.realm) || !namespace_part(copied.title) ||
            !namespace_part(copied.build) || !namespace_part(copied.realm) ||
            copied.realm.find('.') != std::string::npos)
            return OSW_INVALID_ARGUMENT;
        if (!copied.online_id.empty() && !osw::core::valid_online_id(copied.online_id))
            return OSW_INVALID_ARGUMENT;
        const auto diagnostic = std::getenv("OSW_DIAG_ALL_FRIENDS");
        copied.diag_all_friends = diagnostic && std::strcmp(diagnostic, "1") == 0;
        bool expected = false;
        if (!production_context.compare_exchange_strong(expected, true))
            return OSW_LIMIT;
        try {
            *output = osw::core::create_context(std::move(copied), osw::steam::make_backend());
            (*output)->production = true;
        } catch (...) {
            production_context = false;
            throw;
        }
        return OSW_OK;
    } catch (...) {
        return OSW_INVALID_ARGUMENT;
    }
}

int32_t osw_start(osw_context *context) {
    if (!context)
        return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lifecycle(context->lifecycle);
        std::lock_guard lock(context->mutex);
        if (context->started || context->stopping)
            return OSW_INVALID_ARGUMENT;
        context->worker = std::thread([context] { context->run(); });
        context->started = true;
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_create_v2(const osw_config_v2 *config, osw_context **output) {
    if (!output)
        return OSW_INVALID_ARGUMENT;
    *output = nullptr;
    try {
        if (!config || config->base.size != sizeof(*config) ||
            config->base.abi_version != OSW_ABI_VERSION_2 || config->backend > 1 ||
            config->game_port < 1 || config->game_port > 65535)
            return OSW_INVALID_ARGUMENT;
        osw::core::Config copied;
        if (!read_string(config->base.library_path, copied.library_path) ||
            !read_string(config->base.online_id, copied.online_id) ||
            !read_string(config->base.title, copied.title) ||
            !read_string(config->base.build, copied.build) ||
            !read_string(config->base.realm, copied.realm) ||
            !read_string(config->lab_root, copied.lab_root) ||
            !read_string(config->companion_path, copied.companion_path) ||
            !read_string(config->companion_working_dir, copied.companion_working_dir) ||
            !read_string(config->companion_environment, copied.companion_environment) ||
            !read_string(config->game_bind, copied.game_bind) || !namespace_part(copied.title) ||
            !namespace_part(copied.build) || !namespace_part(copied.realm) ||
            copied.realm.find('.') != std::string::npos ||
            !osw::core::loopback_address(copied.game_bind) ||
            (!copied.online_id.empty() && !osw::core::valid_online_id(copied.online_id)))
            return OSW_INVALID_ARGUMENT;
        auto env = nlohmann::json::parse(
            copied.companion_environment.empty() ? "{}" : copied.companion_environment, nullptr,
            false);
        if (!env.is_object() ||
            (config->backend == 1 && (!config->lab_account_id || copied.lab_root.empty())))
            return OSW_INVALID_ARGUMENT;
        copied.backend = config->backend;
        copied.game_port = config->game_port;
        copied.lab_account_id = config->lab_account_id;
        copied.p2p = true;
        if (config->backend == 1) {
            *output = osw::core::create_context(std::move(copied), osw::lab::make_backend());
            return OSW_OK;
        }
        bool expected = false;
        if (!production_context.compare_exchange_strong(expected, true))
            return OSW_LIMIT;
        try {
            *output = osw::core::create_context(std::move(copied), osw::steam::make_backend());
            (*output)->production = true;
        } catch (...) {
            production_context = false;
            throw;
        }
        return OSW_OK;
    } catch (...) {
        return OSW_INVALID_ARGUMENT;
    }
}
int32_t osw_get_identity(osw_context *context, osw_identity *output) {
    if (!context || !output || output->size != sizeof(*output))
        return OSW_INVALID_ARGUMENT;
    try {
        osw::core::Identity identity;
        {
            std::lock_guard lock(context->mutex);
            identity = context->identity;
        }
        *output = {};
        output->size = sizeof(*output);
        output->signed_in = identity.signed_in;
        output->account_id = identity.account_id;
        output->reason_code = identity.reason_code;
        copy_string(output->online_id, identity.online_id);
        copy_string(output->reason, identity.reason);
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_submit(osw_context *context, const osw_request *request, uint64_t *request_id) {
    if (!context || !request || !request_id || request->size != sizeof(*request) ||
        request->operation < OSW_TOKEN || request->operation > OSW_OVERLAY || request->timeout_ms < 1 ||
        request->timeout_ms > 60000 || request->metadata_size > 65536 ||
        request->body_size > 1024 * 1024 || (request->metadata_size && !request->metadata) ||
        (request->body_size && !request->body))
        return OSW_INVALID_ARGUMENT;
    *request_id = 0;
    try {
        auto job = std::make_shared<osw_context::Job>();
        job->request.operation = request->operation;
        job->request.timeout_ms = request->timeout_ms;
        if (request->metadata_size)
            job->request.metadata.assign(request->metadata, request->metadata_size);
        if (request->body_size)
            job->request.body.assign(request->body, request->body + request->body_size);
        job->input_size = request->metadata_size + request->body_size;
        job->deadline = Clock::now() + std::chrono::milliseconds(request->timeout_ms);
        std::lock_guard lock(context->mutex);
        if (!context->started || context->stopping)
            return OSW_SIGNED_OUT;
        if (context->jobs.size() >= max_requests || job->input_size > max_bytes - context->inputs ||
            !context->next_id)
            return OSW_LIMIT;
        auto id = context->next_id++;
        job->request.id = id;
        context->jobs.emplace(id, job);
        try {
            context->queued.push_back(id);
        } catch (...) {
            context->jobs.erase(id);
            throw;
        }
        context->inputs += job->input_size;
        *request_id = id;
        context->wake.notify_one();
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_get_result(osw_context *context, uint64_t id, osw_result *output) {
    if (!context || !output || output->size != sizeof(*output))
        return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(context->mutex);
        auto it = context->jobs.find(id);
        if (it == context->jobs.end())
            return OSW_NOT_FOUND;
        *output = {};
        output->size = sizeof(*output);
        if (!it->second->ready) {
            output->status = OSW_PENDING;
            return OSW_PENDING;
        }
        const auto &result = it->second->result;
        output->status = result.status;
        output->http_status = result.http_status;
        output->metadata = result.metadata.data();
        output->metadata_size = result.metadata.size();
        output->body = result.body.data();
        output->body_size = result.body.size();
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_cancel(osw_context *context, uint64_t id) {
    if (!context)
        return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(context->mutex);
        auto it = context->jobs.find(id);
        if (it == context->jobs.end())
            return OSW_NOT_FOUND;
        if (!it->second->ready) {
            context->complete({id, OSW_ABORTED});
            context->cancellations.push_back(id);
            context->wake.notify_one();
        }
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_release(osw_context *context, uint64_t id) {
    if (!context)
        return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lock(context->mutex);
        auto it = context->jobs.find(id);
        if (it == context->jobs.end())
            return OSW_NOT_FOUND;
        if (!it->second->ready)
            return OSW_PENDING;
        context->outputs -= it->second->result.metadata.size() + it->second->result.body.size();
        context->jobs.erase(it);
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_poll_event(osw_context *context, osw_event *output) {
    if (!context || !output || output->size != sizeof(*output))
        return OSW_INVALID_ARGUMENT;
    try {
        osw::core::Event event;
        {
            std::lock_guard lock(context->mutex);
            if (context->overflow) {
                context->overflow = false;
                event.type = OSW_RESYNC;
            } else if (context->events.empty())
                return OSW_PENDING;
            else {
                event = std::move(context->events.front());
                context->events.pop_front();
            }
        }
        *output = {};
        output->size = sizeof(*output);
        output->type = event.type;
        output->request_id = event.request_id;
        output->account_id = event.account_id;
        output->change_kind = event.change_kind;
        copy_string(output->session_id, event.session_id);
        copy_string(output->invitation_id, event.invitation_id);
        copy_string(output->online_id, event.online_id);
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
int32_t osw_stop(osw_context *context) {
    if (!context)
        return OSW_INVALID_ARGUMENT;
    try {
        std::lock_guard lifecycle(context->lifecycle);
        {
            std::lock_guard lock(context->mutex);
            context->stopping = true;
            context->wake.notify_one();
        }
        context->config.endpoints->close();
        if (context->worker.joinable())
            context->worker.join();
        return OSW_OK;
    } catch (...) {
        return OSW_UNAVAILABLE;
    }
}
void osw_destroy(osw_context *context) {
    if (!context)
        return;
    if (osw_stop(context) != OSW_OK)
        return;
    bool production = context->production;
    delete context;
    if (production)
        production_context = false;
}
}
