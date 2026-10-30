#include "bridge.h"
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint32_t number(const char* argument) {
    std::uint32_t value=0;const char* end=argument+std::strlen(argument);
    const auto parsed=std::from_chars(argument,end,value);
    if(parsed.ec!=std::errc()||parsed.ptr!=end)throw std::runtime_error("argument must be a uint32 decimal integer");
    return value;
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=11&&argc!=12)throw std::runtime_error("usage: native-bridge grid|image RAW WIDTH HEIGHT STRIDE CHANNELS FLAGS MAXCAND MAXSYM MAXATTEMPTS [REPEAT]");
        const std::string kind=argv[1];
        if(kind!="grid"&&kind!="image")throw std::runtime_error("kind must be grid or image");
        std::uint32_t args[8]{};
        for(unsigned i=0;i<8;++i)args[i]=number(argv[3+i]);
        const auto repeat=argc==12?number(argv[11]):1;
        if(!repeat||repeat>10000)throw std::runtime_error("repeat must be 1..10000");
        std::ifstream in(argv[2],std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("cannot open raw input");
        const auto size=in.tellg();
        if(size<=0||size>64*1024*1024)throw std::runtime_error("raw input must contain 1..67108864 bytes");
        const auto length=static_cast<std::uint32_t>(size);
        std::vector<std::uint8_t> bytes(length);in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);
        if(!in)throw std::runtime_error("cannot read raw input");
        std::string output;
        for(std::uint32_t run=0;run<repeat;++run){
            auto* data=specqr_alloc(length);if(!data)throw std::runtime_error("input allocation failed");
            std::memcpy(data,bytes.data(),length);
            output=specqr_decode(kind=="grid"?0:1,data,length,args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7]);
            specqr_free(data);specqr_release_result();
            if(specqr_live_input_bytes()||specqr_result_size())throw std::runtime_error("bridge cleanup failed");
        }
        std::cout<<output<<'\n';
        std::cerr<<"{\"repeat\":"<<repeat<<",\"live_input_bytes\":"<<specqr_live_input_bytes()<<",\"result_bytes\":"<<specqr_result_size()<<"}\n";
        return output.find("\"ok\":true")!=std::string::npos?0:2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}
}
