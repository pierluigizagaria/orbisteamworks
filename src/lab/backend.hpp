#pragma once
#include "core/backend.hpp"
namespace osw::lab {
std::unique_ptr<core::Backend> make_backend();
}
