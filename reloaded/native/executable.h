#pragma once

#include <filesystem>

namespace sparking {
bool supported_executable(const std::filesystem::path& executable);
}
