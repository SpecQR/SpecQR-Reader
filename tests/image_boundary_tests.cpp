#include <specqr/reader.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
unsigned checks=0;
void check(bool okay,const char* label) {
    ++checks;
    if(!okay){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}
}
// Fixed independent qrcode==8.2 output, version 1-L, mask 5, raw bytes
// 00ff014c0d. Original fixture SHA-256:
// 47e4a6b23c98c2efc97beb618d6d5217e8869a3149f14304c222c846d4ad2670
constexpr const char* grid[]={
    "111111100000101111111","100000100010101000001","101110100011101011101",
    "101110101010101011101","101110101100101011101","100000100101001000001",
    "111111101010101111111","000000000000000000000","110001110001000011000",
    "001110001101110111010","010001100010101101011","100101010011111100100",
    "101111111111111111111","000000001010100000100","111111101001010011100",
    "100000101100001001111","101110100001010010110","101110100111111001000",
    "101110100101110111111","100000101111111100100","111111101110100010110"
};
const std::vector<std::uint8_t> payload{0,255,1,76,13};
struct Canvas {
    unsigned width,height,channels,stride;
    std::vector<std::uint8_t> bytes;
    Canvas(unsigned w,unsigned h,unsigned c=1,unsigned padding=0):width(w),height(h),channels(c),stride(w*c+padding),bytes(std::size_t(stride)*h,255){}
    void symbol(unsigned left,unsigned top,unsigned rotation=0,bool mirror=false,bool invert=false,unsigned scale=4) {
        for(unsigned y=0;y<21*scale;++y)for(unsigned x=0;x<21*scale;++x){
            unsigned sx=x/scale,sy=y/scale;
            for(unsigned r=0;r<rotation;++r){const auto old=sx;sx=sy;sy=20-old;}
            if(mirror)sx=20-sx;
            const bool black=(grid[sy][sx]=='1')!=invert;
            for(unsigned c=0;c<channels;++c)bytes[std::size_t(top+y)*stride+(left+x)*channels+c]=black?0:255;
        }
    }
    specqr::ImageView view() const { return {bytes.data(),width,height,stride,channels,bytes.size()}; }
};
void exact(const specqr::ImageResult& result,std::size_t count) {
    check(bool(result),"Image decodes");
    check(result.symbols.size()==count,"Physical symbol count");
    for(const auto& symbol:result.symbols){
        check(symbol.bytes==payload,"Independent exact binary payload");
        check(symbol.rotation%90==0&&symbol.rotation<360,"Sample-grid rotation domain");
        for(auto p:symbol.corners)check(std::isfinite(p.x)&&std::isfinite(p.y),"Finite image corners");
    }
}
}

int main() {
    for(unsigned rotation=0;rotation<4;++rotation)for(bool mirror:{false,true}){
        Canvas canvas(132,128,3,17);canvas.symbol(24,20,rotation,mirror);
        const auto result=specqr::read_image(canvas.view());exact(result,1);
        check(result.symbols[0].mirrored==mirror,"Reflection metadata survives finder normalization");
        check(result.trace.empty()&&result.symbols[0].trace.empty(),"Diagnostic traces opt-in");
        double cx=0,cy=0;for(auto p:result.symbols[0].corners){cx+=p.x/4;cy+=p.y/4;}
        check(std::abs(cx-65.5)<2&&std::abs(cy-61.5)<2,"Affine physical center independent of grid rotation");
        if(mirror){specqr::Options no_mirror;no_mirror.try_mirror=false;check(!specqr::read_image(canvas.view(),no_mirror),"Mirror search option honored");}
    }
    Canvas repeated(330,180);repeated.symbol(24,20);repeated.symbol(218,58,1);
    const auto two=specqr::read_image(repeated.view());exact(two,2);
    double first_x=0,second_x=0;
    for(auto p:two.symbols[0].corners)first_x+=p.x/4;
    for(auto p:two.symbols[1].corners)second_x+=p.x/4;
    check(std::abs(first_x-second_x)>150,"Same-payload symbols retained at distinct physical locations");
    specqr::Options single;single.limits.max_symbols=1;
    const auto limited=specqr::read_image(repeated.view(),single);exact(limited,1);
    check(limited.work_limit_reached,"Partial-success symbol cap exposed");
    specqr::Options candidates;candidates.limits.max_candidates=2;
    const auto clipped=specqr::read_image(repeated.view(),candidates);
    check(!clipped&&clipped.error==specqr::Error::resource_limit&&clipped.work_limit_reached,"Finder cap failure exposed");
    specqr::Options one_attempt;one_attempt.limits.max_grid_attempts=1;
    const auto one=specqr::read_image(repeated.view(),one_attempt);exact(one,1);
    check(one.work_limit_reached,"Shared sampled-grid attempt cap exposed");
    Canvas inverted(132,128);std::fill(inverted.bytes.begin(),inverted.bytes.end(),0);inverted.symbol(24,20,0,false,true);
    specqr::Options inversion;inversion.try_invert=true;
    const auto inverse=specqr::read_image(inverted.view(),inversion);exact(inverse,1);
    check(inverse.symbols[0].inverted,"Inversion metadata preserved");
    Canvas rgb(132,128,3,11);rgb.symbol(24,20);
    // Strongly red light pixels and blue dark pixels exercise the RGB weighting
    // path with padding bytes that are deliberately unrelated to pixel values.
    for(unsigned y=0;y<rgb.height;++y){
        for(unsigned x=0;x<rgb.width;++x){const auto p=std::size_t(y)*rgb.stride+x*3;const bool dark=rgb.bytes[p]==0;rgb.bytes[p]=dark?0:255;rgb.bytes[p+1]=dark?0:150;rgb.bytes[p+2]=dark?180:0;}
        std::fill(rgb.bytes.begin()+std::size_t(y)*rgb.stride+rgb.width*3,rgb.bytes.begin()+std::size_t(y+1)*rgb.stride,0);
    }
    exact(specqr::read_image(rgb.view()),1);
    auto short_view=rgb.view();short_view.data_size=(rgb.height-1)*rgb.stride+rgb.width*3;exact(specqr::read_image(short_view),1);
    --short_view.data_size;check(specqr::read_image(short_view).error==specqr::Error::invalid_input,"Exact last-row extent checked");
    specqr::Options maximum;const auto huge=std::numeric_limits<std::size_t>::max();
    maximum.limits={huge,huge,huge,huge,huge};
    std::uint8_t byte=255;
    check(specqr::read_image({&byte,32769,21,32769,1,32769*21},maximum).error==specqr::Error::resource_limit,"Hard dimension cap before buffer reads");
    check(specqr::read_image({&byte,8193,8193,8193,1,std::size_t(8193)*8193},maximum).error==specqr::Error::resource_limit,"Hard pixel cap before buffer reads");
    check(specqr::read_image({&byte,huge,2,huge,3,huge},maximum).error==specqr::Error::invalid_input,"RGB multiplication overflow rejected");
    for(unsigned value:{0U,127U,255U}){
        Canvas blank(21,21);std::fill(blank.bytes.begin(),blank.bytes.end(),static_cast<std::uint8_t>(value));
        const auto result=specqr::read_image(blank.view(),inversion);
        check(!result&&result.symbols.empty()&&result.error==specqr::Error::not_found,"Uniform minimum-size image has no false decode");
    }
    std::cout<<"image_boundary_tests checks="<<checks<<" passed\n";
}
