#include "internal.hpp"
#include <utility>

namespace specqr { namespace detail {
namespace {
constexpr std::size_t max_data_codewords=2956; // Model 2 version 40, level L.
constexpr char alphanumeric[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
struct Bits {
    const std::vector<std::uint8_t>& data;
    std::size_t pos=0;
    std::size_t remaining() const { return data.size()*8-pos; }
    bool read(unsigned count, unsigned& value) {
        if (count>24 || remaining()<count) return false;
        value=0;
        for (unsigned i=0; i<count; ++i,++pos)
            value=(value<<1)|((data[pos/8]>>(7-pos%8))&1U);
        return true;
    }
};
DecodeResult failure(const char* message, Error error=Error::payload) {
    DecodeResult r; r.error=error; r.message=message; return r;
}
bool valid_tail(Bits& bits, bool terminated, bool& noncanonical) {
    unsigned v=0;
    if (!terminated) return bits.read(static_cast<unsigned>(bits.remaining()),v) && v==0;
    const unsigned align=static_cast<unsigned>((8-bits.pos%8)%8);
    if (!bits.read(align,v) || v!=0) return false;
    // Segno 1.6.6 emits a whole zero byte even if its terminator already
    // lands on a byte boundary. Tolerate precisely this observed variant;
    // preserve the fact for callers selecting strict padding in decode_grid.
    if (align==0 && bits.remaining()>=8 && bits.data[bits.pos/8]==0) {
        bits.read(8,v);
        noncanonical=true;
    }
    unsigned expected=0xec;
    while (bits.remaining()) {
        if (!bits.read(8,v) || v!=expected) return false;
        expected^=0xfd; // 0xec, 0x11, 0xec, ...
    }
    return true;
}
}

DecodeResult parse_payload(const std::vector<std::uint8_t>& data, int version) {
    if (version<1 || version>40 || data.empty()) return failure("Invalid payload version or empty codewords",Error::invalid_input);
    if (data.size()>max_data_codewords) return failure("Payload exceeds Model 2 codeword bound",Error::resource_limit);
    DecodeResult result;
    result.symbol.version=version;
    Bits bits{data};
    int active_eci=-1;
    bool has_data=false;
    const unsigned group=version<=9 ? 0 : version<=26 ? 1 : 2;
    while (bits.remaining()>=4) {
        const std::size_t start=bits.pos;
        unsigned mode=0;
        bits.read(4,mode);
        if (mode==0) {
            if (!valid_tail(bits,true,result.symbol.noncanonical_padding))
                return failure("Invalid zero alignment or alternating pad codewords");
            return result;
        }
        Segment segment;
        segment.mode=static_cast<Mode>(mode);
        segment.bit_offset=start;
        segment.eci=active_eci;
        if (mode==3) {
            if (start!=0 || result.symbol.structured_append.present)
                return failure("Structured Append must be the first and only such header");
            unsigned index=0, count=0, parity=0;
            if (!bits.read(4,index) || !bits.read(4,count) || !bits.read(8,parity))
                return failure("Truncated Structured Append header");
            if (index>count) return failure("Structured Append index exceeds symbol count");
            result.symbol.structured_append={true,index,count+1,parity};
        } else if (mode==5 || mode==9) {
            if (has_data || result.symbol.fnc1!=0)
                return failure("FNC1 must occur once before data");
            result.symbol.fnc1=mode==5 ? 1 : 2;
            if (mode==9) {
                unsigned indicator=0;
                if (!bits.read(8,indicator)) return failure("Truncated FNC1 application indicator");
                result.symbol.fnc1_application_indicator=static_cast<int>(indicator);
            }
        } else if (mode==7) {
            unsigned first=0, rest=0, assignment=0;
            if (!bits.read(8,first)) return failure("Truncated ECI header");
            if ((first&0x80)==0) assignment=first;
            else if ((first&0xc0)==0x80) {
                if (!bits.read(8,rest)) return failure("Truncated two-byte ECI header");
                assignment=((first&0x3f)<<8)|rest;
                if (assignment<128) return failure("Noncanonical two-byte ECI header");
            } else if ((first&0xe0)==0xc0) {
                if (!bits.read(16,rest)) return failure("Truncated three-byte ECI header");
                assignment=((first&0x1f)<<16)|rest;
                if (assignment<16384 || assignment>999999) return failure("ECI assignment outside three-byte range");
            } else return failure("Reserved ECI prefix");
            active_eci=static_cast<int>(assignment);
            segment.eci=active_eci;
        } else if (mode==1 || mode==2 || mode==4 || mode==8 || mode==13) {
            unsigned count_bits=0;
            if (mode==1) { constexpr unsigned widths[]={10,12,14}; count_bits=widths[group]; }
            else if (mode==2) { constexpr unsigned widths[]={9,11,13}; count_bits=widths[group]; }
            else if (mode==4) count_bits=group==0 ? 8 : 16;
            else { constexpr unsigned widths[]={8,10,12}; count_bits=widths[group]; }
            if (mode==13) {
                unsigned subset=0;
                if (!bits.read(4,subset)) return failure("Truncated Hanzi subset");
                if (subset!=1) return failure("Unsupported Hanzi subset",Error::unsupported);
            }
            unsigned count=0;
            if (!bits.read(count_bits,count)) return failure("Truncated character count");
            const std::size_t required=mode==1 ? (count/3)*10+(count%3==1 ? 4 : count%3==2 ? 7 : 0) :
                mode==2 ? (count/2)*11+(count%2)*6 : mode==4 ? count*8 : count*13;
            if (required>bits.remaining()) return failure("Character count exceeds available bits");
            const std::size_t output_size=(mode==8 || mode==13) ? count*2 : count;
            if (output_size>max_data_codewords*3-result.symbol.bytes.size())
                return failure("Decoded bytes exceed bounded output size",Error::resource_limit);
            segment.bytes.reserve(output_size);
            if (mode==1) {
                unsigned left=count;
                while (left) {
                    const unsigned digits=left>=3 ? 3 : left;
                    unsigned value=0;
                    bits.read(digits==3 ? 10 : digits==2 ? 7 : 4,value);
                    const unsigned limit=digits==3 ? 1000 : digits==2 ? 100 : 10;
                    if (value>=limit) return failure("Numeric group exceeds decimal range");
                    unsigned divisor=digits==3 ? 100 : digits==2 ? 10 : 1;
                    for (unsigned i=0; i<digits; ++i) {
                        segment.bytes.push_back(static_cast<std::uint8_t>('0'+value/divisor));
                        value%=divisor; divisor/=10;
                    }
                    left-=digits;
                }
            } else if (mode==2) {
                for (unsigned i=0; i<count/2; ++i) {
                    unsigned value=0; bits.read(11,value);
                    if (value>=45*45) return failure("Alphanumeric pair exceeds alphabet");
                    segment.bytes.push_back(static_cast<std::uint8_t>(alphanumeric[value/45]));
                    segment.bytes.push_back(static_cast<std::uint8_t>(alphanumeric[value%45]));
                }
                if (count%2) {
                    unsigned value=0; bits.read(6,value);
                    if (value>=45) return failure("Alphanumeric character exceeds alphabet");
                    segment.bytes.push_back(static_cast<std::uint8_t>(alphanumeric[value]));
                }
            } else if (mode==4) {
                for (unsigned i=0; i<count; ++i) {
                    unsigned value=0; bits.read(8,value);
                    segment.bytes.push_back(static_cast<std::uint8_t>(value));
                }
            } else {
                for (unsigned i=0; i<count; ++i) {
                    unsigned value=0; bits.read(13,value);
                    unsigned code=0;
                    if (mode==8) {
                        const unsigned reduced=(value/192)*256+value%192;
                        code=reduced+(reduced<0x1f00 ? 0x8140 : 0xc140);
                        const unsigned trail=code&255;
                        if (trail<0x40 || trail>0xfc || trail==0x7f ||
                            !((code>=0x8140 && code<=0x9ffc) || (code>=0xe040 && code<=0xebbf)))
                            return failure("Kanji value outside Shift JIS byte ranges");
                    } else {
                        const unsigned reduced=(value/96)*256+value%96;
                        code=reduced+(reduced<0xa00 ? 0xa1a1 : 0xa6a1);
                        const unsigned trail=code&255;
                        if (trail<0xa1 || trail>0xfe ||
                            !((code>=0xa1a1 && code<=0xaafe) || (code>=0xb0a1 && code<=0xfafe)))
                            return failure("Hanzi value outside GB2312 byte ranges");
                    }
                    segment.bytes.push_back(static_cast<std::uint8_t>(code>>8));
                    segment.bytes.push_back(static_cast<std::uint8_t>(code));
                }
            }
            has_data=true;
            result.symbol.bytes.insert(result.symbol.bytes.end(),segment.bytes.begin(),segment.bytes.end());
        } else return failure("Reserved or unsupported QR mode");
        segment.bit_length=bits.pos-start;
        result.symbol.segments.push_back(std::move(segment));
    }
    if (!valid_tail(bits,false,result.symbol.noncanonical_padding)) return failure("Nonzero shortened terminator");
    return result;
}
} }
