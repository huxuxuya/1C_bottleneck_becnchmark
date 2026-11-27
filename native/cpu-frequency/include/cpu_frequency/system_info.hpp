#pragma once

#include <string>

namespace cpu_frequency {

// Returns a best-effort inventory of resources and hardware exposed to the
// current 1C process. Unavailable platform fields are represented as null.
std::string GetSystemInfoJson();

} // namespace cpu_frequency
