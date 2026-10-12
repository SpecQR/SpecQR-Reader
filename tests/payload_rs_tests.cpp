#include "internal.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::size_t checks=0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
struct Builder {
    std::vector<std::uint8_t> data;
    unsigned bit_count=0;
    void put(unsigned value, unsigned width) {
        for (unsigned n=width; n>0; --n) {
            if (bit_count%8==0) data.push_back(0);
            data.back()|=static_cast<std::uint8_t>(((value>>(n-1))&1U)<<(7-bit_count%8));
            ++bit_count;
        }
    }
    void bytes(const std::vector<std::uint8_t>& bytes, unsigned count_width=8) {
        put(4,4); put(static_cast<unsigned>(bytes.size()),count_width);
        for (unsigned b:bytes) put(b,8);
    }
    void numeric(const std::string& s, unsigned count_width=10) {
        put(1,4); put(static_cast<unsigned>(s.size()),count_width);
        for (std::size_t i=0; i<s.size();) {
            const unsigned n=static_cast<unsigned>(std::min<std::size_t>(3,s.size()-i));
            unsigned v=0;
            for (unsigned j=0; j<n; ++j) v=v*10+static_cast<unsigned>(s[i++]-'0');
            put(v,n==3 ? 10 : n==2 ? 7 : 4);
        }
    }
    void alpha(const std::string& s) {
        const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
        put(2,4); put(static_cast<unsigned>(s.size()),9);
        std::size_t i=0;
        for (; i+1<s.size(); i+=2) put(static_cast<unsigned>(alphabet.find(s[i])*45+alphabet.find(s[i+1])),11);
        if (i<s.size()) put(static_cast<unsigned>(alphabet.find(s[i])),6);
    }
    std::vector<std::uint8_t> finish(unsigned pads=0) {
        put(0,4);
        while (bit_count%8) put(0,1);
        for (unsigned i=0; i<pads; ++i) put(i%2 ? 0x11 : 0xec,8);
        return data;
    }
};
std::vector<std::uint8_t> ascii(const std::string& s) { return {s.begin(),s.end()}; }
void expect_bytes(const Builder& b, const std::vector<std::uint8_t>& expected, int version=1) {
    const auto r=specqr::detail::parse_payload(b.data,version);
    check(bool(r),"Payload accepted");
    check(r.symbol.bytes==expected,"Exact payload bytes");
}
unsigned random_state=0x41b092d3;
unsigned random_u32() {
    random_state^=random_state<<13;
    random_state^=random_state>>17;
    random_state^=random_state<<5;
    return random_state;
}
struct RsVector { unsigned parity; std::vector<std::uint8_t> bytes; };
#include "rs_vectors.inc"

void test_rs() {
    unsigned total_recoveries=0, beyond_rejected=0, beyond_accepted=0;
    for (const auto& vector:rs_vectors) {
        auto clean=vector.bytes;
        unsigned corrected=99;
        check(specqr::detail::correct_rs(clean,vector.parity,corrected),"Independent RS codeword accepted");
        check(clean==vector.bytes && corrected==0,"Clean codeword preserved");
        for (unsigned errors=1; errors<=vector.parity/2; ++errors) {
            for (unsigned trial=0; trial<8; ++trial) {
                auto corrupted=vector.bytes;
                std::vector<unsigned> positions;
                while (positions.size()<errors) {
                    unsigned p=random_u32()%static_cast<unsigned>(corrupted.size());
                    if (trial==0 && positions.empty()) p=0;
                    if (trial==1 && positions.empty()) p=static_cast<unsigned>(corrupted.size()-1);
                    if (std::find(positions.begin(),positions.end(),p)==positions.end()) positions.push_back(p);
                }
                for (auto p:positions) corrupted[p]^=static_cast<std::uint8_t>(1+random_u32()%255);
                check(specqr::detail::correct_rs(corrupted,vector.parity,corrected),"In-radius RS corruption corrected");
                check(corrupted==vector.bytes && corrected==errors,"RS restores exact independent codeword/count");
                ++total_recoveries;
            }
        }
        for (unsigned trial=0; trial<8; ++trial) {
            auto corrupted=vector.bytes;
            for (unsigned i=0; i<vector.parity/2+1; ++i) corrupted[i]^=static_cast<std::uint8_t>(1+random_u32()%255);
            const auto before=corrupted;
            if (specqr::detail::correct_rs(corrupted,vector.parity,corrected)) {
                ++beyond_accepted;
                // Beyond the unique-decoding radius, a different valid codeword
                // is possible. This is measured, never promised to be rejected.
                check(corrected<=vector.parity/2,"RS never reports above-radius correction");
            } else {
                ++beyond_rejected;
                check(corrupted==before && corrected==0,"RS failure is transactional");
            }
        }
    }
    for (auto size:{0U,6U,7U,256U}) {
        std::vector<std::uint8_t> bad(size,0);
        unsigned count=42;
        check(!specqr::detail::correct_rs(bad,7,count) && count==0,"RS length bound");
    }
    for (auto parity:{0U,31U,1000000U}) {
        std::vector<std::uint8_t> bad(32,0);
        unsigned count=42;
        check(!specqr::detail::correct_rs(bad,parity,count) && count==0,"RS parity bound");
    }
    std::cout << "RS independent vectors=" << rs_vectors.size() << " corrected=" << total_recoveries
              << " beyond_radius_rejected=" << beyond_rejected << " beyond_radius_accepted=" << beyond_accepted << '\n';
}
void test_payload() {
    for (const std::string s:{"0","09","001","001234567890"}) {
        Builder b; b.numeric(s); b.finish(3); expect_bytes(b,ascii(s));
    }
    { Builder b; b.alpha("SPECQR 123 $%*+-./:"); b.finish(); expect_bytes(b,ascii("SPECQR 123 $%*+-./:")); }
    { Builder b; b.bytes({0,0xff,0xc0,0x80,0x1d,0xfe}); b.finish(); expect_bytes(b,{0,0xff,0xc0,0x80,0x1d,0xfe}); }
    for (const int version:{9,10,26,27,40}) {
        Builder b; b.numeric("1234567",version<=9 ? 10 : version<=26 ? 12 : 14);
        b.bytes({0,255},version<=9 ? 8 : 16); b.finish();
        auto expected=ascii("1234567"); expected.push_back(0); expected.push_back(255); expect_bytes(b,expected,version);
    }
    for (const int version:{1,10,27}) {
        const unsigned kanji_width=version<=9 ? 8 : version<=26 ? 10 : 12;
        Builder b; b.put(8,4); b.put(1,kanji_width); b.put(0,13);
        b.put(13,4); b.put(1,4); b.put(1,kanji_width); b.put(960,13);
        b.put(2,4); b.put(1,version<=9 ? 9 : version<=26 ? 11 : 13); b.put(10,6); b.finish();
        expect_bytes(b,{0x81,0x40,0xb0,0xa1,'A'},version);
    }
    { Builder b; b.numeric("12"); expect_bytes(b,ascii("12"));
      b.data.back()|=1; check(!specqr::detail::parse_payload(b.data,1),"Nonzero shortened terminator rejected"); }
    { Builder b; b.numeric("123"); expect_bytes(b,ascii("123")); }
    { // Every binary byte must survive unchanged even with no character encoding.
        Builder b; std::vector<std::uint8_t> all; for (unsigned i=0; i<256; ++i) all.push_back(static_cast<std::uint8_t>(i));
        b.bytes(all,16); b.finish(2); expect_bytes(b,all,10);
    }
    for (unsigned assignment:{0U,26U,127U,128U,16383U,16384U,999999U}) {
        Builder b; b.put(7,4);
        if (assignment<128) b.put(assignment,8);
        else if (assignment<16384) b.put(0x8000|assignment,16);
        else b.put(0xc00000|assignment,24);
        b.bytes({0xff,0}); b.finish();
        const auto r=specqr::detail::parse_payload(b.data,1);
        check(bool(r) && r.symbol.bytes==std::vector<std::uint8_t>({255,0}),"ECI bytes preserved");
        check(r.symbol.segments.size()==2 && r.symbol.segments[0].mode==specqr::Mode::eci &&
              r.symbol.segments[0].eci==static_cast<int>(assignment) && r.symbol.segments[1].eci==static_cast<int>(assignment),"ECI state/segment metadata");
    }
    { Builder b; b.put(7,4); b.put(26,8); b.bytes({0xc3,0xa9}); b.put(7,4); b.put(3,8); b.bytes({0xe9}); b.finish();
      const auto r=specqr::detail::parse_payload(b.data,1);
      check(bool(r) && r.symbol.segments[1].eci==26 && r.symbol.segments[3].eci==3,"ECI switching preserved"); }
    { Builder b; b.put(7,4); b.put(26,8); b.put(5,4); b.bytes({0xc3,0xa9}); b.finish();
      const auto r=specqr::detail::parse_payload(b.data,1);
      check(bool(r) && r.symbol.fnc1==1 && r.symbol.segments[2].eci==26,"ECI before FNC1 matches pinned ZXing ordering"); }
    for (unsigned fnc:{5U,9U}) {
        Builder b; b.put(3,4); b.put(1,4); b.put(2,4); b.put(0xa5,8);
        b.put(fnc,4); if (fnc==9) b.put(42,8);
        b.alpha("ABC%12%%34"); b.finish();
        const auto r=specqr::detail::parse_payload(b.data,1);
        check(bool(r) && r.symbol.fnc1==(fnc==5 ? 1 : 2),"FNC1 metadata");
        check(r.symbol.fnc1_application_indicator==(fnc==9 ? 42 : -1),"FNC1 second indicator");
        check(r.symbol.structured_append.present && r.symbol.structured_append.index==1 &&
              r.symbol.structured_append.count==3 && r.symbol.structured_append.parity==0xa5,"Structured Append metadata");
        check(r.symbol.bytes==ascii("ABC%12%%34"),"FNC1 percentages remain raw");
        check(r.symbol.segments[0].bit_offset==0 && r.symbol.segments[0].bit_length==20 &&
              r.symbol.segments[1].bit_offset==20 && r.symbol.segments[1].bit_length==(fnc==9 ? 12 : 4),"Metadata bit spans");
    }
    { Builder b; b.put(8,4); b.put(4,8); for (unsigned value:{0U,5948U,5952U,8191U}) b.put(value,13); b.finish();
      expect_bytes(b,{0x81,0x40,0x9f,0xfc,0xe0,0x40,0xeb,0xbf}); }
    { Builder b; b.put(13,4); b.put(1,4); b.put(4,8); for (unsigned value:{0U,957U,960U,8157U}) b.put(value,13); b.finish();
      expect_bytes(b,{0xa1,0xa1,0xaa,0xfe,0xb0,0xa1,0xfa,0xfe}); }
    // Hand-built malformed bitstreams distinguish numerical validity from
    // simply fitting inside the declared bit width.
    std::vector<Builder> malformed;
    for (unsigned mode:{6U,10U,11U,12U,14U,15U}) { Builder b; b.put(mode,4); b.finish(); malformed.push_back(b); }
    for (auto p:std::vector<std::pair<unsigned,unsigned>>{{1,10},{2,100},{3,1000}}) {
        Builder b; b.put(1,4); b.put(p.first,10); b.put(p.second,p.first==3 ? 10 : p.first==2 ? 7 : 4); b.finish(); malformed.push_back(b);
    }
    { Builder b; b.put(2,4); b.put(2,9); b.put(2025,11); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(2,4); b.put(1,9); b.put(45,6); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(4,4); b.put(255,8); b.finish(); malformed.push_back(b); }
    for (unsigned prefix:{0xe0U,0xffU}) { Builder b; b.put(7,4); b.put(prefix,8); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(7,4); b.put(0x807f,16); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(7,4); b.put(0xc03fff,24); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(7,4); b.put(0xcf4240,24); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(3,4); b.put(15,4); b.put(0,4); b.put(0,8); b.finish(); malformed.push_back(b); }
    { Builder b; b.bytes({1}); b.put(3,4); b.put(0,16); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(5,4); b.put(5,4); b.finish(); malformed.push_back(b); }
    { Builder b; b.bytes({1}); b.put(5,4); b.finish(); malformed.push_back(b); }
    { Builder b; b.put(13,4); b.put(0,4); b.put(0,8); b.finish(); malformed.push_back(b); }
    for (unsigned value:{63U,189U,190U,191U,5949U}) {
        Builder b; b.put(8,4); b.put(1,8); b.put(value,13); b.finish(); malformed.push_back(b);
    }
    for (unsigned value:{94U,95U,958U,959U,8158U,8191U}) {
        Builder b; b.put(13,4); b.put(1,4); b.put(1,8); b.put(value,13); b.finish(); malformed.push_back(b);
    }
    for (const auto& b:malformed) check(!specqr::detail::parse_payload(b.data,1),"Malformed payload rejected");
    { Builder b; b.bytes({1,2,3,4,5,6}); b.finish();
      for (std::size_t n=1; n<7; ++n) check(!specqr::detail::parse_payload({b.data.begin(),b.data.begin()+n},1),"Truncated bytes rejected"); }
    { Builder b; b.bytes({1}); b.finish(2); b.data.back()=0; check(!specqr::detail::parse_payload(b.data,1),"Incorrect padding rejected"); }
    { Builder b; b.bytes({1}); b.finish(); b.data.back()|=1; check(!specqr::detail::parse_payload(b.data,1),"Nonzero alignment rejected"); }
    { Builder b; b.bytes({0,255}); b.finish(2);
      const auto canonical=specqr::detail::parse_payload(b.data,1);
      check(bool(canonical) && !canonical.symbol.noncanonical_padding,"Canonical padding flag clear");
      b.data.insert(b.data.end()-2,0);
      const auto compatible=specqr::detail::parse_payload(b.data,1);
      check(bool(compatible) && compatible.symbol.bytes==std::vector<std::uint8_t>({0,255}) && compatible.symbol.noncanonical_padding,
            "Single aligned Segno zero padding accepted and flagged");
      b.data.insert(b.data.end()-2,0);
      check(!specqr::detail::parse_payload(b.data,1),"Two extra aligned zero pads rejected"); }
    { Builder b; b.numeric("1"); b.finish(2); b.data.insert(b.data.end()-2,0);
      check(!specqr::detail::parse_payload(b.data,1),"Extra zero after unaligned terminator rejected"); }
    check(!specqr::detail::parse_payload({},1),"Empty codewords rejected");
    check(!specqr::detail::parse_payload({0},0),"Version below bound");
    check(!specqr::detail::parse_payload({0},41),"Version above bound");
    check(!specqr::detail::parse_payload(std::vector<std::uint8_t>(2957),40),"Data codewords bound");
    { Builder b; b.numeric(std::string(7000,'0'),14); b.finish(); expect_bytes(b,ascii(std::string(7000,'0')),40); }
    std::cout << "Payload explicit malformed_vectors=" << malformed.size() << '\n';
}
void fuzz_payload_rs() {
    unsigned payload_accepted=0, rs_accepted=0;
    for (unsigned trial=0; trial<20000; ++trial) {
        const std::size_t length=random_u32()%400;
        std::vector<std::uint8_t> bytes(length);
        for (auto& b:bytes) b=static_cast<std::uint8_t>(random_u32());
        const int version=static_cast<int>(random_u32()%44)-1;
        const auto first=specqr::detail::parse_payload(bytes,version);
        const auto second=specqr::detail::parse_payload(bytes,version);
        check(first.error==second.error && first.symbol.bytes==second.symbol.bytes,"Deterministic arbitrary payload parse");
        if (first) { ++payload_accepted; check(first.symbol.bytes.size()<=8868,"Fuzz output bound"); }
        const auto original=bytes;
        unsigned corrected=99;
        if (specqr::detail::correct_rs(bytes,random_u32()%34,corrected)) ++rs_accepted;
        else check(bytes==original && corrected==0,"Fuzz RS rejection preserves input");
    }
    std::cout << "Fuzz deterministic_cases=20000 payload_accepted=" << payload_accepted << " rs_accepted=" << rs_accepted << '\n';
}
}
int main() {
    test_rs(); test_payload(); fuzz_payload_rs();
    std::cout << "payload_rs_tests checks=" << checks << " passed\n";
}
