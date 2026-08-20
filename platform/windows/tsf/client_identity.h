#pragma once

#include "piinput/windows_compat.h"

#include <cstdint>

namespace piinput::windows {

// The id the Host keys its sessions on, together with a per-process session
// counter.
//
// A bare process id is not enough. Windows recycles process ids, the Host runs
// for the whole login, and its session counter restarts at 1 in every process.
// A new process landing on a retired id therefore presents a pair the Host has
// already seen and is handed the dead process's session -- including whether
// that one had been switched to English, which is how a fresh window could come
// up in English against the configured default.
//
// Mixing the process creation time in separates two processes that share an id,
// because they cannot have started at the same 100 ns tick.
[[nodiscard]] std::uint64_t client_id_from(
    std::uint64_t process_id, std::uint64_t creation_time) noexcept;

// The same, for the running process. Stable for the life of the process.
[[nodiscard]] std::uint64_t process_client_id() noexcept;

}  // namespace piinput::windows
