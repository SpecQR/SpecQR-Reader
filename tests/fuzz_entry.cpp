#include "specqr/reader.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
// Optional libFuzzer target. Product code does not require a fuzzing runtime.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t length) {
    if(length<4)return 0;
    specqr::Options options;options.limits.max_candidates=16;options.limits.max_grid_attempts=32;
    options.try_invert=data[0]&1;options.try_mirror=data[0]&2;options.diagnostics=data[0]&4;
    if(data[0]&8) {
        std::size_t n=17+4*(1+data[1]%40);
        specqr::decode_grid({data+4,n,n,length-4},options);
    } else {
        std::size_t width=1+data[1],height=1+data[2],channels=(data[3]&1)?3:1;
        specqr::read_image({data+4,width,height,width*channels,channels,length-4},options);
    }
    return 0;
}
