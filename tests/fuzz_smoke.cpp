#include <specqr/reader.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
// Deterministic malformed-input smoke fuzzer; this is not exhaustive fuzz proof.
int main(int argc,char** argv) {
    unsigned iterations=argc>1?static_cast<unsigned>(std::stoul(argv[1])):20000;
    if(iterations>1000000)return 2;
    std::mt19937 rng(0x53515231);std::size_t successes=0;
    specqr::Options opts;opts.limits.max_candidates=16;opts.limits.max_grid_attempts=32;
    for(unsigned i=0;i<iterations;++i) {
        std::size_t n=21+4*(rng()%40), stride=n+(rng()%3);
        std::vector<std::uint8_t> data(stride*n);
        for(auto& b:data)b=static_cast<std::uint8_t>(rng()&1);
        if(i%5==0)data[rng()%data.size()]=255;
        auto length=i%7==0?rng()%data.size():data.size();
        auto r=specqr::decode_grid({data.data(),n,stride,length},opts);
        successes+=bool(r);
        if(r && (r.symbol.version<1||r.symbol.version>40||r.symbol.bytes.size()>7089))return 3;
        if(i%20==0) {
            std::size_t w=21+rng()%160,h=21+rng()%160,ch=i%40?1:3;
            std::vector<std::uint8_t> pixels(w*h*ch);
            for(auto& p:pixels)p=static_cast<std::uint8_t>(rng());
            auto ir=specqr::read_image({pixels.data(),w,h,w*ch,ch,pixels.size()},opts);
            successes+=ir.symbols.size();
            if(!ir.symbols.empty())return 4;
        }
    }
    std::cout<<"{\"seed\":1397838385,\"random_grids\":"<<iterations<<",\"random_images\":"<<((iterations+19)/20)<<",\"false_decodes\":"<<successes<<"}\n";
    return successes?1:0;
}
