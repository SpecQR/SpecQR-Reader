#ifdef NDEBUG
#undef NDEBUG
#endif
#include <specqr/reader.hpp>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>
int main() {
    using namespace specqr;
    const auto huge=std::numeric_limits<std::size_t>::max();
    std::vector<std::uint8_t> g(21*21), im(64*64,255);
    assert(decode_grid({}).error==Error::invalid_input);
    assert(decode_grid({g.data(),huge,huge,g.size()}).error==Error::invalid_input);
    assert(decode_grid({g.data(),21,huge,g.size()}).error==Error::invalid_input);
    assert(decode_grid({g.data(),21,20,g.size()}).error==Error::invalid_input);
    assert(decode_grid({g.data(),21,21,g.size()-1}).error==Error::invalid_input);
    g[30]=2;assert(decode_grid({g.data(),21,21,g.size()}).error==Error::invalid_input);g[30]=0;
    Options zero;zero.limits.max_grid_attempts=0;
    assert(decode_grid({g.data(),21,21,g.size()},zero).error==Error::resource_limit);
    Options tiny;tiny.limits.max_pixels=440;
    assert(decode_grid({g.data(),21,21,g.size()},tiny).error==Error::resource_limit);
    assert(!decode_grid({g.data(),21,21,g.size()}));
    assert(read_image({}).error==Error::invalid_input);
    assert(!read_image({im.data(),huge,huge,huge,3,im.size()}));
    assert(read_image({im.data(),64,64,huge,1,im.size()}).error==Error::invalid_input);
    assert(read_image({im.data(),64,64,64,2,im.size()}).error==Error::invalid_input);
    assert(read_image({im.data(),64,64,64,1,im.size()-1}).error==Error::invalid_input);
    assert(read_image({im.data(),64,64,64,1,im.size()},tiny).error==Error::resource_limit);
    assert(read_image({im.data(),64,64,64,1,im.size()},zero).error==Error::resource_limit);
    auto blank=read_image({im.data(),64,64,64,1,im.size()});
    assert(!blank && blank.symbols.empty() && blank.trace.empty());
    std::cout<<"API boundary assertions passed\n";
}
