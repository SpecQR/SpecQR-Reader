#include <specqr/reader.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
std::string quote(const std::string& s) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') { out+='\\'; out+=char(c); }
        else if(c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
std::string hex(const std::vector<std::uint8_t>& v) {
    const char* digits="0123456789abcdef"; std::string out; out.reserve(v.size()*2);
    for(auto c:v) {out+=digits[c>>4];out+=digits[c&15];} return out;
}
void traces(const std::vector<specqr::Trace>& ts) {
    std::cout<<'['; bool first=true;
    for(const auto& t:ts) {if(!first)std::cout<<',';first=false;std::cout<<"{\"stage\":"<<quote(t.stage)<<",\"detail\":"<<quote(t.detail)<<'}';}
    std::cout<<']';
}
void symbol(const specqr::Symbol& s) {
    std::cout<<"{\"bytes_hex\":"<<quote(hex(s.bytes))<<",\"hex\":"<<quote(hex(s.bytes))
        <<",\"version\":"<<s.version<<",\"mask\":"<<s.mask<<",\"ec_level\":"<<quote(std::string(1,s.ec_level))
        <<",\"corrected_codewords\":"<<s.corrected_codewords<<",\"format_distance\":"<<s.format_distance
        <<",\"rotation\":"<<s.rotation<<",\"mirrored\":"<<(s.mirrored?"true":"false")<<",\"inverted\":"<<(s.inverted?"true":"false")
        <<",\"noncanonical_padding\":"<<(s.noncanonical_padding?"true":"false")
        <<",\"fnc1\":"<<s.fnc1<<",\"fnc1_application_indicator\":"<<s.fnc1_application_indicator
        <<",\"structured_append\":{\"present\":"<<(s.structured_append.present?"true":"false")
        <<",\"index\":"<<s.structured_append.index<<",\"count\":"<<s.structured_append.count<<",\"parity\":"<<s.structured_append.parity<<"},\"segments\":[";
    bool first=true;
    for(const auto& seg:s.segments) {
        if(!first)std::cout<<',';
        first=false;
        std::cout<<"{\"mode\":"<<unsigned(seg.mode)<<",\"bytes_hex\":"<<quote(hex(seg.bytes))
            <<",\"hex\":"<<quote(hex(seg.bytes))<<",\"eci\":"<<seg.eci
            <<",\"bit_offset\":"<<seg.bit_offset<<",\"bit_length\":"<<seg.bit_length<<'}';
    }
    std::cout<<"],\"corners\":[";
    for(std::size_t i=0;i<4;++i) {if(i)std::cout<<',';std::cout<<'['<<s.corners[i].x<<','<<s.corners[i].y<<']';}
    std::cout<<"],\"trace\":";traces(s.trace);std::cout<<'}';
}
std::size_t number(const std::string& s,std::size_t max) {
    if(s.empty())throw std::runtime_error("missing unsigned integer");
    std::size_t v=0;
    for(char c:s) {if(c<'0'||c>'9'||v>max/10||(v==max/10&&std::size_t(c-'0')>max%10))throw std::runtime_error("invalid or oversized integer"); v=v*10+std::size_t(c-'0');}
    return v;
}
std::size_t process_peak_kib() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");std::string line;
    while(std::getline(status,line)) {
        if(line.compare(0,6,"VmHWM:")==0) {
            std::istringstream field(line.substr(6));std::size_t value=0;std::string unit;
            if(field>>value>>unit && unit=="kB")return value;
        }
    }
#endif
    return 0;
}
struct Pnm {
    std::vector<std::uint8_t> file; std::size_t pos=0,w=0,h=0,channels=0;
    std::string token() {
        for(;;) {
            while(pos<file.size()&&(file[pos]==' '||file[pos]=='\n'||file[pos]=='\r'||file[pos]=='\t'))++pos;
            if(pos<file.size()&&file[pos]=='#') {while(pos<file.size()&&file[pos]!='\n')++pos;} else break;
        }
        std::string s;
        while(pos<file.size()&&file[pos]!=' '&&file[pos]!='\n'&&file[pos]!='\r'&&file[pos]!='\t'&&file[pos]!='#') {
            if(s.size()>=32)throw std::runtime_error("PNM token too long");
            s+=char(file[pos++]);
        }
        return s;
    }
    specqr::ImageView load(const std::string& path) {
        std::ifstream in(path,std::ios::binary|std::ios::ate); if(!in)throw std::runtime_error("cannot open image");
        auto len=in.tellg(); if(len<0||len>64*1024*1024)throw std::runtime_error("image file exceeds 64 MiB adapter limit");
        file.resize(static_cast<std::size_t>(len));in.seekg(0);in.read(reinterpret_cast<char*>(file.data()),len);
        if(!in)throw std::runtime_error("cannot read image");
        const auto magic=token();if(magic!="P5"&&magic!="P6")throw std::runtime_error("adapter requires binary P5/P6 PNM");
        channels=magic=="P6"?3:1;w=number(token(),8192);h=number(token(),8192);
        if(number(token(),65535)!=255)throw std::runtime_error("PNM maxval must be 255");
        if(pos>=file.size()||(file[pos]!=' '&&file[pos]!='\n'&&file[pos]!='\r'&&file[pos]!='\t'))throw std::runtime_error("missing PNM raster separator");
        if(file[pos++]=='\r') {
            if(pos>=file.size()||file[pos]!='\n')throw std::runtime_error("PNM CR raster separator must be CRLF");
            ++pos;
        }
        if(w==0||h==0||w*h>16*1024*1024||file.size()-pos!=w*h*channels)throw std::runtime_error("invalid PNM dimensions or raster size");
        return {file.data()+pos,w,h,w*channels,channels,file.size()-pos};
    }
};
}
int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("usage: specqr-reader grid|image FILE [--trace] [--invert] [--repeat N]");
        specqr::Options options;std::size_t repeat=1;
        for(int i=3;i<argc;++i) {
            std::string a=argv[i];if(a=="--trace")options.diagnostics=true;else if(a=="--invert")options.try_invert=true;else if(a=="--strict-padding")options.strict_padding=true;
            else if(a=="--repeat"&&i+1<argc) {repeat=number(argv[++i],10000);if(!repeat)throw std::runtime_error("repeat must be positive");}
            else throw std::runtime_error("unknown argument");
        }
        std::vector<std::uint8_t> grid;specqr::GridView gv;Pnm pnm;specqr::ImageView iv;const std::string kind=argv[1];
        if(kind=="grid") {
            std::ifstream in(argv[2]);if(!in)throw std::runtime_error("cannot open grid");
            std::string line;for(;;) {int c=in.get();if(c=='\n')break;if(c==EOF||line.size()>=5)throw std::runtime_error("invalid grid size");line+=char(c);}
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            auto n=number(line,177);
            if(n<21||(n-21)%4)throw std::runtime_error("invalid QR Model 2 grid dimension");
            grid.reserve(n*n);for(std::size_t y=0;y<n;++y) {
                // Bound each line before allocation, including hostile very long records.
                for(std::size_t x=0;x<n;++x) {int c=in.get();if(c!='0'&&c!='1')throw std::runtime_error("grid rows require exactly N binary modules");grid.push_back(std::uint8_t(c-'0'));}
                int c=in.get();if(c=='\r')c=in.get();if(c!='\n'&&!(c==EOF&&y+1==n))throw std::runtime_error("invalid grid row end");
            }
            if(in.peek()!=EOF)throw std::runtime_error("trailing grid bytes");
            gv={grid.data(),n,n,grid.size()};
        } else if(kind=="image")iv=pnm.load(argv[2]);else throw std::runtime_error("mode must be grid or image");
        specqr::ImageResult result;const auto begin=std::chrono::steady_clock::now();
        for(std::size_t run=0;run<repeat;++run) {
            if(kind=="image")result=specqr::read_image(iv,options);
            else {auto r=specqr::decode_grid(gv,options);result={};result.error=r.error;result.message=r.message;if(r)result.symbols.push_back(std::move(r.symbol));}
        }
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/double(repeat);
        const auto peak=process_peak_kib();
        std::cout<<"{\"ok\":"<<(result?"true":"false")<<",\"error\":"<<quote(specqr::error_name(result.error))
            <<",\"message\":"<<quote(result.message)<<",\"runtime_ms\":"<<ms<<",\"repeat\":"<<repeat<<",\"peak_rss_kib\":"<<(peak?std::to_string(peak):"null")<<",\"work_limit_reached\":"<<(result.work_limit_reached?"true":"false")<<",\"symbols\":[";
        for(std::size_t i=0;i<result.symbols.size();++i) {if(i)std::cout<<',';symbol(result.symbols[i]);}
        std::cout<<"],\"trace\":";traces(result.trace);std::cout<<"}\n";return result?0:2;
    } catch(const std::exception& e) {std::cout<<"{\"ok\":false,\"error\":\"invalid_input\",\"message\":"<<quote(e.what())<<",\"symbols\":[]}\n";return 2;}
}
