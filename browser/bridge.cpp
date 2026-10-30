#include "bridge.h"
#include <specqr/reader.hpp>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>
#include <string>

namespace {
constexpr std::uint32_t max_input_bytes=64U*1024U*1024U;
std::uint8_t* live_input=nullptr;
std::uint32_t live_input_bytes=0;
std::string result_storage;
const char* result_pointer=nullptr;
std::uint32_t result_length=0;

// These complete, allocation-free responses are also safe when constructing an
// API error result or serializing a successful result throws std::bad_alloc.
constexpr char allocation_error[]="{\"ok\":false,\"error\":\"resource_limit\",\"message\":\"Browser adapter allocation failed\",\"work_limit_reached\":false,\"symbols\":[],\"trace\":[]}";
constexpr char unexpected_error[]="{\"ok\":false,\"error\":\"adapter_error\",\"message\":\"Unexpected exception in browser adapter\",\"work_limit_reached\":false,\"symbols\":[],\"trace\":[]}";

std::string quote(const std::string& s) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') {out+='\\';out+=char(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
std::string hex(const std::vector<std::uint8_t>& v) {
    const char* digits="0123456789abcdef";std::string out;out.reserve(v.size()*2);
    for(auto c:v){out+=digits[c>>4];out+=digits[c&15];}return out;
}
void traces(std::ostream& out,const std::vector<specqr::Trace>& ts) {
    out<<'[';bool first=true;
    for(const auto& t:ts){if(!first)out<<',';first=false;out<<"{\"stage\":"<<quote(t.stage)<<",\"detail\":"<<quote(t.detail)<<'}';}
    out<<']';
}
// Matches tools/reader_cli.cpp's field names, ordering, and numeric precision.
void symbol(std::ostream& out,const specqr::Symbol& s) {
    out<<"{\"bytes_hex\":"<<quote(hex(s.bytes))<<",\"hex\":"<<quote(hex(s.bytes))
        <<",\"version\":"<<s.version<<",\"mask\":"<<s.mask<<",\"ec_level\":"<<quote(std::string(1,s.ec_level))
        <<",\"corrected_codewords\":"<<s.corrected_codewords<<",\"format_distance\":"<<s.format_distance
        <<",\"rotation\":"<<s.rotation<<",\"mirrored\":"<<(s.mirrored?"true":"false")<<",\"inverted\":"<<(s.inverted?"true":"false")
        <<",\"noncanonical_padding\":"<<(s.noncanonical_padding?"true":"false")
        <<",\"fnc1\":"<<s.fnc1<<",\"fnc1_application_indicator\":"<<s.fnc1_application_indicator
        <<",\"structured_append\":{\"present\":"<<(s.structured_append.present?"true":"false")
        <<",\"index\":"<<s.structured_append.index<<",\"count\":"<<s.structured_append.count<<",\"parity\":"<<s.structured_append.parity<<"},\"segments\":[";
    bool first=true;
    for(const auto& seg:s.segments){
        if(!first)out<<',';first=false;
        out<<"{\"mode\":"<<unsigned(seg.mode)<<",\"bytes_hex\":"<<quote(hex(seg.bytes))
            <<",\"hex\":"<<quote(hex(seg.bytes))<<",\"eci\":"<<seg.eci
            <<",\"bit_offset\":"<<seg.bit_offset<<",\"bit_length\":"<<seg.bit_length<<'}';
    }
    out<<"],\"corners\":[";
    for(std::size_t i=0;i<4;++i){if(i)out<<',';out<<'['<<s.corners[i].x<<','<<s.corners[i].y<<']';}
    out<<"],\"trace\":";traces(out,s.trace);out<<'}';
}
std::string serialize(const specqr::ImageResult& r) {
    std::ostringstream out;
    // basic_ostream can otherwise swallow allocation failures and leave a
    // truncated JSON document instead of reaching our allocation-free fallback.
    out.exceptions(std::ios::badbit|std::ios::failbit);
    out<<"{\"ok\":"<<(r?"true":"false")<<",\"error\":"<<quote(specqr::error_name(r.error))
        <<",\"message\":"<<quote(r.message)<<",\"work_limit_reached\":"<<(r.work_limit_reached?"true":"false")<<",\"symbols\":[";
    for(std::size_t i=0;i<r.symbols.size();++i){if(i)out<<',';symbol(out,r.symbols[i]);}
    out<<"],\"trace\":";traces(out,r.trace);out<<'}';
    return out.str();
}
specqr::ImageResult invalid(const char* message) {
    specqr::ImageResult r;r.error=specqr::Error::invalid_input;r.message=message;return r;
}
const char* fallback(const char* text) noexcept {
    result_pointer=text;result_length=static_cast<std::uint32_t>(std::strlen(text));return text;
}
}

extern "C" {
std::uint8_t* specqr_alloc(std::uint32_t byte_length) noexcept {
    if(live_input||byte_length==0||byte_length>max_input_bytes)return nullptr;
    live_input=static_cast<std::uint8_t*>(std::malloc(byte_length));
    if(live_input)live_input_bytes=byte_length;
    return live_input;
}
void specqr_free(std::uint8_t* input) noexcept {
    if(input&&input==live_input){std::free(live_input);live_input=nullptr;live_input_bytes=0;}
}
std::uint32_t specqr_live_input_bytes() noexcept {return live_input_bytes;}
void specqr_release_result() noexcept {
    std::string().swap(result_storage);result_pointer=nullptr;result_length=0;
}
std::uint32_t specqr_result_size() noexcept {return result_length;}
const char* specqr_decode(std::uint32_t kind,const std::uint8_t* input,
    std::uint32_t byte_length,std::uint32_t width,std::uint32_t height,
    std::uint32_t stride,std::uint32_t channels,std::uint32_t flags,
    std::uint32_t max_candidates,std::uint32_t max_symbols,
    std::uint32_t max_grid_attempts) noexcept {
    specqr_release_result();
    try {
        specqr::ImageResult r;
        if(!input||input!=live_input||!byte_length||byte_length>live_input_bytes)
            r=invalid("Input pointer and length must refer to the live browser adapter allocation");
        else if(kind>1)r=invalid("Browser adapter kind must be zero (grid) or one (image)");
        else if(flags&~15U)r=invalid("Browser adapter flags contain unsupported bits");
        else {
            specqr::Options options;
            options.diagnostics=(flags&1U)!=0;options.try_invert=(flags&2U)!=0;
            options.strict_padding=(flags&4U)!=0;options.try_mirror=(flags&8U)==0;
            if(max_candidates)options.limits.max_candidates=max_candidates;
            if(max_symbols)options.limits.max_symbols=max_symbols;
            if(max_grid_attempts)options.limits.max_grid_attempts=max_grid_attempts;
            if(kind==1)r=specqr::read_image({input,width,height,stride,channels,byte_length},options);
            else {
                auto decoded=specqr::decode_grid({input,width,stride,byte_length},options);
                r.error=decoded.error;r.message=std::move(decoded.message);
                if(decoded)r.symbols.push_back(std::move(decoded.symbol));
            }
        }
        result_storage=serialize(r);
        result_pointer=result_storage.c_str();
        result_length=static_cast<std::uint32_t>(result_storage.size());
        return result_pointer;
    } catch(const std::bad_alloc&) {return fallback(allocation_error);}
      catch(...) {return fallback(unexpected_error);}
}
}
