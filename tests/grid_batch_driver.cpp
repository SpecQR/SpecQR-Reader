// Optional test adapter. Reads a bounded Model 2 grid for each stdin record.
#include "specqr/reader.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
int main() {
    unsigned size=0;
    while (std::cin>>size) {
        if (size<21 || size>177 || (size-17)%4) return 2;
        std::vector<std::uint8_t> data;
        data.reserve(size*size);
        for (unsigned y=0; y<size; ++y) {
            std::string row;
            if (!(std::cin>>row) || row.size()!=size) return 3;
            for (char c:row) {
                if (c!='0' && c!='1') return 4;
                data.push_back(static_cast<std::uint8_t>(c-'0'));
            }
        }
        specqr::Options options;
        options.try_invert=true;
        auto begin=std::chrono::steady_clock::now();
        auto result=specqr::decode_grid({data.data(),size,size,data.size()},options);
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"{\"ok\":"<<(result?"true":"false")<<",\"error\":"<<unsigned(result.error)
                 <<",\"version\":"<<result.symbol.version<<",\"mask\":"<<result.symbol.mask
                 <<",\"ec_level\":\""<<result.symbol.ec_level<<"\",\"rotation\":"<<result.symbol.rotation
                 <<",\"mirrored\":"<<(result.symbol.mirrored?"true":"false")
                 <<",\"inverted\":"<<(result.symbol.inverted?"true":"false")
                 <<",\"format_distance\":"<<result.symbol.format_distance
                 <<",\"corrected_codewords\":"<<result.symbol.corrected_codewords
                 <<",\"runtime_ms\":"<<elapsed<<",\"hex\":\"";
        constexpr char hex[]="0123456789abcdef";
        for (auto byte:result.symbol.bytes) std::cout<<hex[byte>>4]<<hex[byte&15];
        std::cout<<"\"}"<<std::endl;
    }
}
