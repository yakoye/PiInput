#include "client_identity.h"

namespace piinput::windows {

std::uint64_t client_id_from(
    const std::uint64_t process_id, const std::uint64_t creation_time) noexcept {
    // A plain concatenation would truncate one of the two halves; the creation
    // time alone repeats across processes started in the same tick. Mixing both
    // through a bit-avalanche keeps every input bit affecting the result.
    std::uint64_t mixed = creation_time ^ (process_id * 0x9E3779B97F4A7C15ULL);
    mixed *= 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 31U;
    // Zero is how the protocol spells "no client", and is rejected on arrival.
    return mixed == 0U ? 1U : mixed;
}

std::uint64_t process_client_id() noexcept {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    std::uint64_t born = 0U;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE) {
        born = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) |
            created.dwLowDateTime;
    }
    return client_id_from(static_cast<std::uint64_t>(GetCurrentProcessId()), born);
}

}  // namespace piinput::windows
