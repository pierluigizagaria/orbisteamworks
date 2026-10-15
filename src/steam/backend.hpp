#pragma once
#include "core/backend.hpp"

namespace osw::steam {
std::unique_ptr<core::Backend> make_backend();
}
