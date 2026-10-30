#pragma once
#include <cstdint>

// ABI v1: wasm32 unsigned integers. JavaScript callers must validate integer
// ranges before calling: an out-of-range JS number otherwise silently wraps.
// Exactly one input allocation may be live. Allocation failure returns nullptr.
// Freeing an unrecognized pointer is a no-op. The input cap is 64 MiB.
extern "C" {
std::uint8_t* specqr_alloc(std::uint32_t byte_length) noexcept;
void specqr_free(std::uint8_t* input) noexcept;
std::uint32_t specqr_live_input_bytes() noexcept;

// kind=0: GridView {input,width,stride,byte_length}; height/channels ignored.
// kind=1: ImageView {input,width,height,stride,channels,byte_length}.
// flags: 1 diagnostics, 2 try_invert, 4 strict_padding, 8 disable_mirror.
// Each zero work limit selects the public API default; positive values override.
// Pointer/extent must refer to the current specqr_alloc allocation. The returned
// JSON pointer belongs to this module, and survives only until the next decode
// or release_result. UTF-8 JSON retains raw bytes_hex and complete metadata.
const char* specqr_decode(std::uint32_t kind, const std::uint8_t* input,
    std::uint32_t byte_length, std::uint32_t width, std::uint32_t height,
    std::uint32_t stride, std::uint32_t channels, std::uint32_t flags,
    std::uint32_t max_candidates, std::uint32_t max_symbols,
    std::uint32_t max_grid_attempts) noexcept;
std::uint32_t specqr_result_size() noexcept;
void specqr_release_result() noexcept;
}
