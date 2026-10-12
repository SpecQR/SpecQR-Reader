#include <specqr/reader.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct ExpectedSegment { unsigned mode;std::string hex;std::size_t offset,length; };
struct FncCase {
    std::string name;bool valid;int position,indicator,version;std::string hex;
    std::vector<std::string> rows;std::vector<ExpectedSegment> segments;
};
#include "fnc1_vectors.inc"
unsigned checks=0;
void check(bool okay,const std::string& message) {
    ++checks;if(!okay){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
std::vector<std::uint8_t> bytes(const std::string& hex) {
    std::vector<std::uint8_t> result;
    for(std::size_t i=0;i<hex.size();i+=2)
        result.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
    return result;
}
void verify(const FncCase& c,const specqr::Symbol& symbol) {
    check(symbol.bytes==bytes(c.hex),c.name+" raw aggregate bytes");
    check(symbol.fnc1==c.position,c.name+" FNC1 position");
    check(symbol.fnc1_application_indicator==c.indicator,c.name+" raw application indicator");
    check(symbol.version==c.version&&symbol.ec_level=='Q',c.name+" version/error level");
    check(symbol.segments.size()==c.segments.size(),c.name+" segment count");
    for(std::size_t i=0;i<c.segments.size();++i){
        const auto& actual=symbol.segments[i];const auto& expected=c.segments[i];
        check(static_cast<unsigned>(actual.mode)==expected.mode,c.name+" segment mode");
        check(actual.bytes==bytes(expected.hex),c.name+" exact segment bytes");
        check(actual.eci==-1,c.name+" unspecified ECI");
        check(actual.bit_offset==expected.offset&&actual.bit_length==expected.length,c.name+" segment bit boundary");
    }
    check(!symbol.structured_append.present,c.name+" no invented Structured Append");
    check(!symbol.noncanonical_padding,c.name+" external encoder canonical padding");
}
}

int main() {
    unsigned first=0,second=0,negative=0;
    for(const auto& c:fnc_cases){
        const std::size_t n=c.rows.size();std::vector<std::uint8_t> modules;
        for(const auto& row:c.rows)for(char ch:row)modules.push_back(static_cast<std::uint8_t>(ch-'0'));
        specqr::Options options;options.strict_padding=true;
        const auto grid=specqr::decode_grid({modules.data(),n,n,modules.size()},options);
        check(bool(grid)==c.valid,c.name+" public grid acceptance");
        if(c.valid)verify(c,grid.symbol);
        else check(grid.error==specqr::Error::payload,c.name+" malformed grid header rejected as payload");
        const std::size_t size=(n+8)*4;
        for(std::size_t channels:{1U,3U}){
            const std::size_t stride=size*channels+7;std::vector<std::uint8_t> raster(stride*size,255);
            for(std::size_t y=0;y<n;++y)for(std::size_t x=0;x<n;++x)
                for(std::size_t dy=0;dy<4;++dy)for(std::size_t dx=0;dx<4;++dx)
                    for(std::size_t channel=0;channel<channels;++channel)
                        raster[((y+4)*4+dy)*stride+((x+4)*4+dx)*channels+channel]=modules[y*n+x]?0:255;
            const auto image=specqr::read_image({raster.data(),size,size,stride,channels,raster.size()},options);
            check(bool(image)==c.valid,c.name+" public image acceptance");
            check(image.symbols.size()==(c.valid?1U:0U),c.name+" image symbol count");
            if(c.valid)verify(c,image.symbols.front());
        }
        if(!c.valid)++negative;else if(c.position==1)++first;else ++second;
    }
    std::cout<<"FNC1 public API checks="<<checks<<" passed; independently encoded FIRST="<<first
             <<" SECOND="<<second<<" malformed="<<negative<<"; each grid+gray+RGB\n";
}
