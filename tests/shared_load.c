/* Loads the orbisteamworks shared library at runtime, as a C host would, and
 * checks the signed-out contract when the Steam API library is absent. It
 * never links the library; osw.h supplies only types and constants. */
#include "orbisteamworks/osw.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE Module;
static Module load_module(const char *path) { return LoadLibraryA(path); }
static void *find(Module module, const char *name) { return (void *)GetProcAddress(module, name); }
static void unload_module(Module module) { FreeLibrary(module); }
static void pause_ms(unsigned ms) { Sleep(ms); }
#else
#include <dlfcn.h>
#include <time.h>
typedef void *Module;
static Module load_module(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *find(Module module, const char *name) { return dlsym(module, name); }
static void unload_module(Module module) { dlclose(module); }
static void pause_ms(unsigned ms) {
    struct timespec delay = {0, (long)ms * 1000000L};
    nanosleep(&delay, NULL);
}
#endif

typedef struct Api {
    int32_t (*create)(const osw_config *, osw_context **);
    int32_t (*start)(osw_context *);
    int32_t (*get_identity)(osw_context *, osw_identity *);
    int32_t (*submit)(osw_context *, const osw_request *, uint64_t *);
    int32_t (*get_result)(osw_context *, uint64_t, osw_result *);
    int32_t (*cancel)(osw_context *, uint64_t);
    int32_t (*release)(osw_context *, uint64_t);
    int32_t (*poll_event)(osw_context *, osw_event *);
    int32_t (*stop)(osw_context *);
    void (*destroy)(osw_context *);
    int32_t (*create_v2)(const osw_config_v2 *, osw_context **);
    int32_t (*enable_virtual_endpoints)(osw_context *);
    int32_t (*get_local_endpoint)(osw_context *, osw_endpoint *);
    int32_t (*route_endpoint)(osw_context *, uint32_t, const osw_endpoint *, osw_endpoint *);
} Api;

static const char *const names[] = {"osw_create",  "osw_start",      "osw_get_identity",
                                    "osw_submit",  "osw_get_result", "osw_cancel",
                                    "osw_release", "osw_poll_event", "osw_stop",
                                    "osw_destroy", "osw_create_v2", "osw_enable_virtual_endpoints",
                                    "osw_get_local_endpoint", "osw_route_endpoint", "osw_enable_loopback_endpoints",
                                    "osw_set_app_id", "osw_get_persona_name"};
#define NAME_COUNT (sizeof(names) / sizeof(names[0]))

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

#ifdef _WIN32
/* Every exported name must belong to the C API: no C++ symbol may leak. */
static int exports_only_api(Module module) {
    const unsigned char *base = (const unsigned char *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY *exports;
    const DWORD *exported;
    DWORD i;
    if (!dir->VirtualAddress)
        return 0;
    exports = (const IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
    exported = (const DWORD *)(base + exports->AddressOfNames);
    if (exports->NumberOfFunctions != NAME_COUNT || exports->NumberOfNames != NAME_COUNT)
        return 0;
    for (i = 0; i < exports->NumberOfNames; ++i)
        if (strncmp((const char *)(base + exported[i]), "osw_", 4) != 0)
            return 0;
    return 1;
}
#endif

static int run(const Api *api, const char *steam_library) {
    osw_config config;
    osw_context *context = NULL;
    osw_identity identity;
    osw_event event;
    osw_request request;
    osw_result result;
    uint64_t id = 0;
    const char metadata[] = "{\"client_id\":\"fixture\",\"scope\":\"openid\",\"server_id\":\"2\"}";
    const char *error = NULL;
    int waited, seen = 0;
    int32_t status = OSW_PENDING;

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.abi_version = OSW_ABI_VERSION + 1;
    if (strlen(steam_library) >= sizeof(config.library_path))
        return fail("Steam library path too long");
    strcpy(config.library_path, steam_library);
    strcpy(config.online_id, "OswShared");
    strcpy(config.title, "osw-shared-test");
    strcpy(config.build, "1");
    strcpy(config.realm, "offline");
    if (api->create(&config, &context) != OSW_INVALID_ARGUMENT || context)
        return fail("an unknown ABI version must be refused");
    config.abi_version = OSW_ABI_VERSION;
    if (api->create(&config, &context) != OSW_OK || !context)
        return fail("create");
    if (api->start(context) != OSW_OK) {
        error = "start";
        goto done;
    }

    memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    for (waited = 0; waited < 5000 && !seen; waited += 10) {
        while (!seen && api->poll_event(context, &event) == OSW_OK)
            seen = event.type == OSW_IDENTITY_CHANGED;
        if (!seen)
            pause_ms(10);
    }
    if (!seen) {
        error = "no identity event within 5 seconds";
        goto done;
    }
    memset(&identity, 0, sizeof(identity));
    identity.size = sizeof(identity);
    if (api->get_identity(context, &identity) != OSW_OK) {
        error = "get_identity";
        goto done;
    }
    if (identity.signed_in || identity.account_id || identity.reason_code != 10 ||
        !identity.reason[0]) {
        error = "expected signed out with reason_code 10 and a reason";
        goto done;
    }
    printf("signed out: reason_code=%d reason=\"%s\"\n", identity.reason_code, identity.reason);

    /* A signed-out library must refuse or fail a token request, never succeed. */
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.operation = OSW_TOKEN;
    request.timeout_ms = 2000;
    request.metadata = metadata;
    request.metadata_size = sizeof(metadata) - 1;
    status = api->submit(context, &request, &id);
    if (status == OSW_OK) {
        memset(&result, 0, sizeof(result));
        result.size = sizeof(result);
        status = OSW_PENDING;
        for (waited = 0; waited < 5000 && status == OSW_PENDING; waited += 10) {
            status = api->get_result(context, id, &result);
            if (status == OSW_PENDING)
                pause_ms(10);
        }
        if (status != OSW_OK)
            error = "token request did not complete";
        else if (result.status == OSW_OK || result.body_size)
            error = "token request must not succeed while signed out";
        else if (api->release(context, id) != OSW_OK)
            error = "release";
    } else if (status != OSW_SIGNED_OUT)
        error = "token submission must report signed out";

done:
    if (api->stop(context) != OSW_OK && !error)
        error = "stop";
    api->destroy(context);
    return error ? fail(error) : 0;
}

int main(int argc, char **argv) {
    Module module;
    Api api;
    void *slots[NAME_COUNT];
    size_t i;
    int code;
    if (argc != 3)
        return fail("usage: osw-shared-load-test <orbisteamworks library> <absent "
                    "steam library>");
    module = load_module(argv[1]);
    if (!module)
        return fail("cannot load the orbisteamworks shared library");
    for (i = 0; i < NAME_COUNT; ++i)
        if (!(slots[i] = find(module, names[i]))) {
            fprintf(stderr, "missing export %s\n", names[i]);
            unload_module(module);
            return fail("export resolution");
        }
#ifdef _WIN32
    if (!exports_only_api(module)) {
        unload_module(module);
        return fail("the DLL exports names outside the osw_* C API");
    }
#endif
    /* Object and function pointers have the same representation on the
     * supported Windows and POSIX hosts; this mirrors a host loader shim. */
    memcpy(&api.create, &slots[0], sizeof(void *));
    memcpy(&api.start, &slots[1], sizeof(void *));
    memcpy(&api.get_identity, &slots[2], sizeof(void *));
    memcpy(&api.submit, &slots[3], sizeof(void *));
    memcpy(&api.get_result, &slots[4], sizeof(void *));
    memcpy(&api.cancel, &slots[5], sizeof(void *));
    memcpy(&api.release, &slots[6], sizeof(void *));
    memcpy(&api.poll_event, &slots[7], sizeof(void *));
    memcpy(&api.stop, &slots[8], sizeof(void *));
    memcpy(&api.destroy, &slots[9], sizeof(void *));
    memcpy(&api.create_v2, &slots[10], sizeof(void *));
    {
        osw_config_v2 invalid = {0};
        osw_context *context = NULL;
        if (api.create_v2(&invalid, &context) != OSW_INVALID_ARGUMENT || context != NULL) {
            unload_module(module);
            return fail("ABI 2 rejects invalid configuration");
        }
    }
    code = run(&api, argv[2]);
    unload_module(module);
    if (!code)
        printf("PASS: runtime-loaded C API reports signed out without Steam\n");
    return code;
}
