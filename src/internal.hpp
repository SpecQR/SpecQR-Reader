#pragma once
#include "specqr/reader.hpp"
namespace specqr { namespace detail {
bool correct_rs(std::vector<std::uint8_t>& block, unsigned parity, unsigned& corrected);
DecodeResult parse_payload(const std::vector<std::uint8_t>& data, int version);
} }
