#include "orbisteamworks/osw.h"

int osw_header_c_translation_unit(void) {
    osw_config config = {0};
    osw_request request = {0};
    osw_event event = {0};
    config.size = sizeof(config);
    config.abi_version = OSW_ABI_VERSION;
    request.size = sizeof(request);
    request.operation = OSW_WEBAPI;
    event.size = sizeof(event);
    event.change_kind = 1;
    return config.abi_version == 1 && request.operation == OSW_WEBAPI && event.change_kind == 1;
}
