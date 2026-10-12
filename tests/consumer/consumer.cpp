#include <specqr/reader.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <vector>

// Built only against a relocated installation; no private header is used.
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::ifstream input(argv[1]);
    std::size_t n = 0;
    if (!(input >> n) || n != 21) return 2;
    std::vector<std::uint8_t> modules(n*n);
    for (auto& bit : modules) {
        char c = 0;
        if (!(input >> c) || (c != '0' && c != '1')) return 3;
        bit = static_cast<std::uint8_t>(c-'0');
    }
    const std::vector<std::uint8_t> expected{0xc8,0x79,0xd7,0x93,0xd7};
    auto decoded = specqr::decode_grid({modules.data(),n,n,modules.size()});
    if (!decoded || decoded.symbol.bytes != expected || decoded.symbol.version != 1 ||
        decoded.symbol.mask != 0 || decoded.symbol.ec_level != 'H' ||
        !decoded.symbol.noncanonical_padding) return 4;
    specqr::Options strict;
    strict.strict_padding = true;
    if (specqr::decode_grid({modules.data(),n,n,modules.size()},strict).error != specqr::Error::payload) return 5;
    std::array<std::uint8_t,21*21> blank{};
    if (specqr::decode_grid({blank.data(),21,21,blank.size()})) return 6;
    const std::size_t scale=5, side=(n+8)*scale;
    std::vector<std::uint8_t> pixels(side*side,255);
    for (std::size_t y=0; y<n; ++y)
        for (std::size_t x=0; x<n; ++x)
            for (std::size_t dy=0; dy<scale; ++dy)
                for (std::size_t dx=0; dx<scale; ++dx)
                    pixels[((y+4)*scale+dy)*side+(x+4)*scale+dx]=modules[y*n+x]?0:255;
    auto image = specqr::read_image({pixels.data(),side,side,side,1,pixels.size()});
    if (!image || image.symbols.size()!=1 || image.symbols[0].bytes!=expected || image.work_limit_reached) return 7;
    std::cout << "Installed public consumer: exact grid bytes/metadata, strict rejection, blank rejection and exact image bytes passed\n";
    return 0;
}
