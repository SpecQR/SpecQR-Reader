#include "bridge.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// Deterministic exhaustion exercises errors both inside Reader and while the
// adapter serializes/copies results. No fault hook is shipped in the Wasm ABI.
namespace {long fail_after=-1;}
void* operator new(std::size_t n) {
    if(fail_after==0)throw std::bad_alloc();
    if(fail_after>0)--fail_after;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream in(argv[1]);unsigned n=0;in>>n;
    std::vector<std::uint8_t> grid;std::string row;
    while(in>>row)for(char c:row)grid.push_back(static_cast<std::uint8_t>(c-'0'));
    assert(grid.size()==n*n);
    assert(!specqr_alloc(0));assert(!specqr_alloc(67108865));assert(!specqr_alloc(0xffffffffU));
    auto* input=specqr_alloc(static_cast<std::uint32_t>(grid.size()));assert(input);
    std::memcpy(input,grid.data(),grid.size());
    assert(specqr_live_input_bytes()==grid.size());assert(!specqr_alloc(1));
    specqr_free(input+1);assert(specqr_live_input_bytes()==grid.size());
    const auto length=static_cast<std::uint32_t>(grid.size());
    auto decode=[&](unsigned kind,unsigned flags,unsigned width,const std::uint8_t* pointer,std::uint32_t size){
        return specqr_decode(kind,pointer,size,width,n,n,1,flags,0,0,0);
    };
    assert(std::strstr(decode(0,0,n,input,length),"\"ok\":true"));
    assert(std::strstr(decode(0,0,n,input,length-1),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(0,0,n,input,length+1),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(0,0,n,input+1,length),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(0,0,n,nullptr,length),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(0,0,0xffffffffU,input,length),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(9,0,n,input,length),"\"error\":\"invalid_input\""));
    assert(std::strstr(decode(0,16,n,input,length),"\"error\":\"invalid_input\""));
    unsigned faults=0,successes=0;
    for(long budget=0;budget<512;++budget){
        fail_after=budget;
        const char* result=decode(0,1,n,input,length);
        // No allocating operation is needed to verify a complete fallback.
        assert(result&&std::strlen(result)==specqr_result_size());
        assert(result[0]=='{'&&result[specqr_result_size()-1]=='}');
        fail_after=-1;
        if(std::strstr(result,"\"ok\":true"))++successes;
        else {++faults;assert(std::strstr(result,"\"error\":\"resource_limit\""));}
        std::cout<<result<<'\n'; // Parse each line with an independent JSON parser.
        specqr_release_result();assert(specqr_result_size()==0);
        assert(specqr_live_input_bytes()==length);
    }
    assert(faults>0&&successes>0);
    specqr_free(input);specqr_free(input);specqr_release_result();
    assert(!specqr_live_input_bytes()&&!specqr_result_size());
    std::cerr<<"{\"fault_injection_cases\":512,\"resource_limit_results\":"<<faults
             <<",\"successful_results\":"<<successes<<",\"live_input_bytes\":"<<specqr_live_input_bytes()
             <<",\"result_bytes\":"<<specqr_result_size()<<",\"contract\":\"passed\"}\n";
}
