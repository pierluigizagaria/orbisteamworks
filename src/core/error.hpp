#pragma once
#include <stdexcept>
#include <string>

namespace osw {
enum class ErrorCode {
    library_missing = 10,
    symbol_missing,
    client_unavailable,
    signed_out,
    server_init,
    server_logon,
    timeout,
    ticket,
    token,
    io,
    arguments,
    auth_rejected,
    callback_abi,
    library_load,
    steam_shutdown
};
class Error : public std::runtime_error {
  public:
    Error(ErrorCode code, const std::string &message) : std::runtime_error(message), code(code) {}
    ErrorCode code;
};
} // namespace osw
