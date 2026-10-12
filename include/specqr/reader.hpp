#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace specqr {
enum class Error { none, invalid_input, resource_limit, not_found, format, version, ecc, payload, unsupported };
enum class Mode : unsigned { numeric=1, alphanumeric=2, structured_append=3, byte=4, fnc1_first=5, eci=7, kanji=8, fnc1_second=9, hanzi=13 };
struct Segment {
    Mode mode = Mode::byte;
    std::vector<std::uint8_t> bytes;
    int eci = -1; // -1 means default/unspecified, never a guessed encoding.
    std::size_t bit_offset = 0, bit_length = 0;
};
struct StructuredAppend { bool present=false; unsigned index=0, count=0, parity=0; };
struct Point { double x=0, y=0; };
struct Trace { std::string stage, detail; };
struct Symbol {
    std::vector<std::uint8_t> bytes;
    std::vector<Segment> segments;
    StructuredAppend structured_append;
    int fnc1 = 0; // 0 absent, 1 first position, 2 second position.
    int fnc1_application_indicator = -1;
    int version=0, mask=0; char ec_level='?';
    unsigned corrected_codewords=0, format_distance=0;
    unsigned rotation=0; bool mirrored=false, inverted=false;
    bool noncanonical_padding=false; // Narrow Segno 1.6.6 extra-zero compatibility.
    std::array<Point,4> corners{};
    std::vector<Trace> trace;
};
struct Limits {
    std::size_t max_pixels=16*1024*1024, max_dimension=8192;
    std::size_t max_candidates=96, max_symbols=32, max_grid_attempts=2048;
};
struct Options {
    Limits limits{};
    bool try_mirror=true, try_invert=false, diagnostics=false;
    bool strict_padding=false; // Reject the documented extra-zero padding accommodation.
};
// One byte per module, exactly 0=light or 1=dark; no quiet zone.
struct GridView { const std::uint8_t* data=nullptr; std::size_t size=0, stride=0, data_size=0; };
// Interleaved gray (channels=1) or RGB (channels=3), top-to-bottom.
struct ImageView { const std::uint8_t* data=nullptr; std::size_t width=0,height=0,stride=0,channels=1,data_size=0; };
struct DecodeResult { Error error=Error::none; std::string message; Symbol symbol; explicit operator bool() const { return error==Error::none; } };
struct ImageResult { Error error=Error::none; std::string message; std::vector<Symbol> symbols; std::vector<Trace> trace; bool work_limit_reached=false; explicit operator bool() const { return error==Error::none; } };
// Allocation failures are reported as resource_limit where caught. These APIs are
// not noexcept: constructing/copying an error result may itself throw std::bad_alloc.
DecodeResult decode_grid(GridView grid, const Options& options = {});
ImageResult read_image(ImageView image, const Options& options = {});
const char* error_name(Error error);
} // namespace specqr
