#ifndef ORBISTEAMWORKS_OSW_H
#define ORBISTEAMWORKS_OSW_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSW_ABI_VERSION 1u
#define OSW_ABI_VERSION_2 2u
typedef struct osw_context osw_context;
typedef struct osw_endpoint {
    uint32_t size;
    char address[16]; /* Canonical dotted IPv4; copied, never a borrowed pointer. */
    uint32_t port;    /* Host byte order, 1..65535. */
} osw_endpoint;
enum osw_route_direction { OSW_ROUTE_OUTBOUND = 1, OSW_ROUTE_INBOUND = 2 };
enum osw_status {
    OSW_OK = 0,
    OSW_PENDING = 1,
    OSW_INVALID_ARGUMENT = -1,
    OSW_SIGNED_OUT = -2,
    OSW_UNAVAILABLE = -3,
    OSW_NOT_SUPPORTED = -4,
    OSW_ABORTED = -5,
    OSW_TIMEOUT = -6,
    OSW_LIMIT = -7,
    OSW_NOT_FOUND = -8
};
enum osw_operation {
    OSW_TOKEN = 1,
    OSW_WEBAPI = 2,
    OSW_FRIENDS = 3,
    OSW_INVITE_SEND = 4,
    OSW_INVITE_ACCEPT = 5,
    OSW_INVITE_DECLINE = 6,
    OSW_RESOLVE_NAME = 7,
    OSW_P2P = 8,
    OSW_OVERLAY = 9
};
enum osw_event_type {
    OSW_IDENTITY_CHANGED = 1,
    OSW_REQUEST_COMPLETE = 2,
    OSW_FRIENDS_CHANGED = 3,
    OSW_INVITATION_RECEIVED = 4,
    OSW_INVITATION_ACCEPTED = 5,
    OSW_SESSION_CHANGED = 6,
    OSW_RESYNC = 7,
    OSW_OVERLAY_CHANGED = 8
};

typedef struct osw_config {
    uint32_t size, abi_version;
    char library_path[1024];
    char online_id[17];
    char title[17];
    char build[65];
    char realm[65];
} osw_config;

/* Explicit opt-in extension. ABI 1 and osw_create retain their exact layout.
 * Strings are UTF-8. companion_environment is a JSON object of env strings.
 * Lab mode is a simulation, never a fallback for unavailable Steam. */
typedef struct osw_config_v2 {
    osw_config base;    /* base.size = sizeof(osw_config_v2), base.abi_version = 2 */
    uint32_t backend;   /* 0 = Steam, 1 = simulated local lab */
    uint32_t game_port; /* 1..65535, normally 3074 */
    uint64_t lab_account_id;
    char lab_root[1024];
    char companion_path[1024];
    char companion_working_dir[1024];
    char companion_environment[8192];
    char game_bind[16]; /* exact IPv4 loopback address; no wildcard */
} osw_config_v2;

typedef struct osw_identity {
    uint32_t size;
    int32_t signed_in, reason_code;
    uint64_t account_id;
    char online_id[17];
    char reason[160];
} osw_identity;

/* Metadata is UTF-8 JSON. The body is opaque binary. Both are copied on entry.
 * Operation schemas are documented in docs/C_API.md. No credential is logged.
 */
typedef struct osw_request {
    uint32_t size, operation, timeout_ms;
    const char *metadata;
    size_t metadata_size;
    const uint8_t *body;
    size_t body_size;
} osw_request;

/* Pointers remain valid until osw_release for this request, or osw_destroy.
 * The host must not release/destroy while another host thread reads them. */
typedef struct osw_result {
    uint32_t size;
    int32_t status, http_status;
    const char *metadata;
    size_t metadata_size;
    const uint8_t *body;
    size_t body_size;
} osw_result;

typedef struct osw_event {
    uint32_t size, type;
    uint64_t request_id, account_id;
    char session_id[32], invitation_id[65], online_id[17];
    uint32_t change_kind; /* Session: 1 joined, 2 left, 3 metadata.
                            Friend: 1 in-game, 2 removed/offline, 3 name changed;
                            account_id=0 is a generic refresh. Overlay: 0 closed, 1 active. */
} osw_event;

int32_t osw_create(const osw_config *config, osw_context **output);
/* Additive ABI: explicit P2P opt-in before start; ABI 1/2 layouts stay unchanged.
 * Endpoint structures must have size=sizeof(osw_endpoint). Route getters copy
 * cached values under a short mutex, with no allocation or provider/network wait.
 * OUTBOUND maps an authorized peer's canonical endpoint to a local UDP proxy;
 * INBOUND maps that proxy source to the peer's canonical endpoint. Unknown or
 * revoked routes return NOT_FOUND. Errors leave output unchanged. Input/output
 * may alias. No API call may race destroy. See docs/C_API.md for lifecycle. */
int32_t osw_enable_virtual_endpoints(osw_context *context);
/* Mutually exclusive with virtual mode; fixed canonical loopback sockets need
 * no host translation. Both mode selections are immutable before/after start. */
int32_t osw_enable_loopback_endpoints(osw_context *context);
/* Pre-start only, nonzero AppID. Default 480; central auth helper stays 480. */
int32_t osw_set_app_id(osw_context *context, uint32_t app_id);
/* Raw provider persona, copied from the worker snapshot. required includes NUL.
 * NULL/zero buffer queries required and returns LIMIT; short buffers unchanged.
 * No provider/network call. Explicit online_id never changes the raw persona. */
int32_t osw_get_persona_name(osw_context *context, char *buffer, size_t capacity, size_t *required);
int32_t osw_get_local_endpoint(osw_context *context, osw_endpoint *output);
int32_t osw_route_endpoint(osw_context *context, uint32_t direction,
                         const osw_endpoint *input, osw_endpoint *output);
int32_t osw_create_v2(const osw_config_v2 *config, osw_context **output);
int32_t osw_start(osw_context *context);
int32_t osw_get_identity(osw_context *context, osw_identity *output);
int32_t osw_submit(osw_context *context, const osw_request *request, uint64_t *request_id);
int32_t osw_get_result(osw_context *context, uint64_t request_id, osw_result *output);
int32_t osw_cancel(osw_context *context, uint64_t request_id);
int32_t osw_release(osw_context *context, uint64_t request_id);
int32_t osw_poll_event(osw_context *context, osw_event *output);
/* Stop joins the owner thread and completes pending requests with OSW_ABORTED.
 * Completed results survive stop. Destroy must not race any other API call. */
int32_t osw_stop(osw_context *context);
void osw_destroy(osw_context *context);

#ifdef __cplusplus
}
#endif
#endif
