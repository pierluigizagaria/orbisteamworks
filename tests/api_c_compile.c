#include "orbisteamworks/osw.h"
typedef char abi_1_config_layout[sizeof(osw_config) == 1196 ? 1 : -1];
typedef char abi_2_config_layout[sizeof(osw_config_v2) == 12496 ? 1 : -1];
typedef char abi_2_identity_alignment[offsetof(osw_config_v2, lab_account_id) == 1208 ? 1 : -1];
typedef char endpoint_layout[sizeof(osw_endpoint) == 24 ? 1 : -1];

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
