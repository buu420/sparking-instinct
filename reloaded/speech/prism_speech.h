#pragma once

#include <filesystem>
#include <string_view>

namespace sparking::speech {

// Starts the worker and returns before Prism loads. The window starts unfocused.
// Pass an absolute DLL path and, optionally, a dedicated Prism diagnostic log.
bool start(const std::filesystem::path& dll, const std::filesystem::path& log = {}) noexcept;
bool say(std::string_view text, bool interrupt = true) noexcept;
void stop() noexcept;
bool speaking() noexcept;
void set_foreground(bool foreground) noexcept;

// Call only during normal teardown, outside DllMain and outside the game tick.
void shutdown() noexcept;

}
