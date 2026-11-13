#include "executable.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>

namespace sparking {
namespace {
constexpr std::uintmax_t executable_size = 149726792;
constexpr std::array<unsigned char, 32> executable_sha256{
    0x6d, 0xb7, 0x00, 0x2a, 0x8f, 0xa6, 0x6a, 0x13,
    0x09, 0xb0, 0x10, 0x37, 0xd8, 0xa9, 0xa8, 0xc6,
    0x78, 0xa8, 0xec, 0xd3, 0x5b, 0x72, 0xd4, 0xc6,
    0xaf, 0xac, 0x55, 0x0a, 0x74, 0x37, 0x16, 0x57
};
}

bool supported_executable(const std::filesystem::path& executable)
{
    std::error_code error;
    if (_wcsicmp(executable.filename().c_str(), L"SparkingZERO-Win64-Shipping.exe") != 0 ||
        std::filesystem::file_size(executable, error) != executable_size || error) return false;
    std::ifstream file(executable, std::ios::binary);
    if (!file) return false;

    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    BCRYPT_HASH_HANDLE hash{};
    bool success = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 32> actual{};
    if (success) {
        std::array<unsigned char, 64 * 1024> bytes{};
        while (file && success) {
            file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (const auto count = file.gcount(); count > 0)
                success = BCryptHashData(hash, bytes.data(), static_cast<ULONG>(count), 0) >= 0;
        }
        success = success && file.eof() && !file.bad() &&
            BCryptFinishHash(hash, actual.data(), static_cast<ULONG>(actual.size()), 0) >= 0;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return success && actual == executable_sha256;
}
}
