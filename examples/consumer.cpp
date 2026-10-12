#include <specqr/reader.hpp>
#include <array>
#include <iostream>
#include <fstream>
#include <vector>
int main(int argc,char** argv) {
    std::array<std::uint8_t, 21*21> empty{};
    auto r = specqr::decode_grid({empty.data(),21,21,empty.size()});
    if (r) { std::cerr << "blank grid incorrectly accepted\n"; return 1; }
    std::cout << "External public API consumer: blank grid rejected as "
              << specqr::error_name(r.error) << '\n';
    if(argc>1) {
        std::ifstream input(argv[1]);std::size_t n=0;
        if(!(input>>n)||n<21||n>177)return 2;
        std::vector<std::uint8_t> modules;modules.reserve(n*n);
        for(std::size_t i=0;i<n*n;++i) {char c=0;if(!(input>>c)||(c!='0'&&c!='1'))return 2;modules.push_back(std::uint8_t(c-'0'));}
        auto decoded=specqr::decode_grid({modules.data(),n,n,modules.size()});
        if(!decoded) {std::cerr<<decoded.message<<'\n';return 3;}
        const char* digits="0123456789abcdef";
        std::cout<<"Exact payload hex: ";
        for(auto b:decoded.symbol.bytes)std::cout<<digits[b>>4]<<digits[b&15];
        std::cout<<'\n';
    }
    return 0;
}
