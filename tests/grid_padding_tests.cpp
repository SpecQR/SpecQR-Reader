#include "specqr/reader.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    assert(argc==2);
    std::ifstream input(argv[1]);
    std::size_t size=0;
    input>>size;
    assert(size>=21 && size<=177);
    std::vector<std::uint8_t> modules;
    for (std::size_t y=0; y<size; ++y) {
        std::string row;
        input>>row;
        assert(row.size()==size);
        for (char c:row) { assert(c=='0' || c=='1'); modules.push_back(static_cast<std::uint8_t>(c-'0')); }
    }
    specqr::GridView grid{modules.data(),size,size,modules.size()};
    auto default_result=specqr::decode_grid(grid);
    assert(default_result && default_result.symbol.noncanonical_padding);
    assert(default_result.symbol.trace.empty());
    specqr::Options strict;
    strict.strict_padding=true;
    auto strict_result=specqr::decode_grid(grid,strict);
    assert(!strict_result && strict_result.error==specqr::Error::payload);
    specqr::Options diagnostics;
    diagnostics.diagnostics=true;
    auto traced_result=specqr::decode_grid(grid,diagnostics);
    assert(traced_result && traced_result.symbol.bytes==default_result.symbol.bytes);
    bool compatibility=false;
    for (const auto& event:traced_result.symbol.trace)
        compatibility=compatibility || event.stage=="compatibility";
    assert(compatibility);
    std::cout<<"Grid default/strict padding and diagnostic assertions passed\n";
}
