#include "internal.hpp"
#include "qr_tables.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace specqr {
namespace {
DecodeResult failure(Error error, const char* message) {
    DecodeResult out;
    out.error = error;
    out.message = message;
    return out;
}

unsigned weight(unsigned value) {
    unsigned count = 0;
    while (value) { value &= value - 1; ++count; }
    return count;
}

// Polynomial long division over GF(2). The leading bit of generator is degree.
unsigned bch_word(unsigned data, unsigned degree, unsigned generator) {
    unsigned word = data << degree;
    unsigned remainder = word;
    for (int bit = 31; bit >= static_cast<int>(degree); --bit)
        if ((remainder >> bit) & 1U)
            remainder ^= generator << (static_cast<unsigned>(bit) - degree);
    return word | remainder;
}

struct CanonicalGrid {
    GridView input;
    unsigned rotation;
    bool mirror, invert;
    unsigned at(int x, int y) const {
        const int end = static_cast<int>(input.size) - 1;
        int source_x=x, source_y=y;
        switch (rotation) {
        case 1: source_x=y; source_y=end-x; break;
        case 2: source_x=end-x; source_y=end-y; break;
        case 3: source_x=end-y; source_y=x; break;
        default: break;
        }
        if (mirror) source_x = end-source_x;
        return input.data[static_cast<std::size_t>(source_y)*input.stride +
                          static_cast<std::size_t>(source_x)] ^ unsigned(invert);
    }
};

// Decode against all BCH words (32 for format, 34 for version). Both copies
// participate; a minimum-distance tie is rejected instead of guessed.
int nearest_bch(unsigned first, unsigned second, bool format, unsigned& distance) {
    int selected = -1;
    bool tied = false;
    distance = 32;
    const unsigned begin = format ? 0 : 7, end = format ? 32 : 41;
    for (unsigned value=begin; value<end; ++value) {
        const unsigned encoded = format ? (bch_word(value,10,0x537)^0x5412)
                                        : bch_word(value,12,0x1f25);
        const unsigned current = std::min(weight(first^encoded), weight(second^encoded));
        if (current < distance) { distance=current; selected=static_cast<int>(value); tied=false; }
        else if (current == distance) tied=true;
    }
    return distance<=3 && !tied ? selected : -1;
}

bool data_mask(int pattern, int x, int y) {
    switch (pattern) {
    case 0: return (x+y)%2==0;
    case 1: return y%2==0;
    case 2: return x%3==0;
    case 3: return (x+y)%3==0;
    case 4: return (y/2+x/3)%2==0;
    case 5: return (x*y)%2+(x*y)%3==0;
    case 6: return ((x*y)%2+(x*y)%3)%2==0;
    case 7: return ((x+y)%2+(x*y)%3)%2==0;
    default: return false;
    }
}

std::vector<std::uint8_t> function_modules(int version, int size) {
    std::vector<std::uint8_t> reserved(static_cast<std::size_t>(size*size),0);
    auto mark = [&](int x, int y) { reserved[static_cast<std::size_t>(y*size+x)]=1; };
    auto rectangle = [&](int x0, int y0, int width, int height) {
        for (int y=y0; y<y0+height; ++y)
            for (int x=x0; x<x0+width; ++x) mark(x,y);
    };
    // Finder patterns, their separators, format words, and the fixed dark module.
    rectangle(0,0,9,9);
    rectangle(size-8,0,8,9);
    rectangle(0,size-8,9,8);
    for (int index=0; index<size; ++index) { mark(6,index); mark(index,6); }
    const auto& centers=detail::alignment_centers[version-1];
    for (unsigned row=0; row<8 && centers[row]; ++row)
        for (unsigned col=0; col<8 && centers[col]; ++col) {
            const int x=centers[col], y=centers[row];
            if ((x==6 && (y==6 || y==size-7)) || (y==6 && x==size-7)) continue;
            rectangle(x-2,y-2,5,5);
        }
    if (version>=7) {
        rectangle(size-11,0,3,6);
        rectangle(0,size-11,6,3);
    }
    return reserved;
}

DecodeResult canonical_decode(const CanonicalGrid& grid, bool diagnostics, bool strict_padding) {
    const int size=static_cast<int>(grid.input.size), version=(size-17)/4;
    unsigned first=0, second=0;
    for (int i=0; i<15; ++i) {
        int x,y;
        if (i<6) { x=8; y=i; }
        else if (i==6) { x=8; y=7; }
        else if (i==7) { x=8; y=8; }
        else if (i==8) { x=7; y=8; }
        else { x=14-i; y=8; }
        first |= grid.at(x,y)<<i;
        x=i<8 ? size-1-i : 8;
        y=i<8 ? 8 : size-15+i;
        second |= grid.at(x,y)<<i;
    }
    unsigned format_distance=0;
    const int format=nearest_bch(first,second,true,format_distance);
    if (format<0) return failure(Error::format,"Format BCH words are uncorrectable or ambiguous");
    unsigned version_distance=0;
    if (version>=7) {
        first=second=0;
        for (int i=0; i<18; ++i) {
            const int x=size-11+i%3, y=i/3;
            first |= grid.at(x,y)<<i;
            second |= grid.at(y,x)<<i;
        }
        const int decoded=nearest_bch(first,second,false,version_distance);
        if (decoded!=version)
            return failure(Error::version,"Version BCH words do not identify the supplied dimension");
    }
    const int mask=format&7;
    // Format bits encode M=00, L=01, H=10, Q=11. Tables use L,M,Q,H.
    constexpr int level_index[4]={1,0,3,2};
    constexpr char levels[4]={'L','M','Q','H'};
    const int level=level_index[format>>3];
    const auto params=detail::block_parameters[version-1][level];
    const std::size_t count=detail::total_codewords[version-1];
    const auto reserved=function_modules(version,size);
    std::vector<std::uint8_t> words(count,0);
    std::size_t bit=0, remainder_nonzero=0;
    bool upward=true;
    for (int right=size-1; right>0; right-=2) {
        if (right==6) --right;
        for (int offset=0; offset<size; ++offset) {
            const int y=upward ? size-1-offset : offset;
            for (int side=0; side<2; ++side) {
                const int x=right-side;
                if (reserved[static_cast<std::size_t>(y*size+x)]) continue;
                const unsigned value=grid.at(x,y)^unsigned(data_mask(mask,x,y));
                if (bit<count*8) words[bit/8] |= static_cast<std::uint8_t>(value<<(7-bit%8));
                else remainder_nonzero += value;
                ++bit;
            }
        }
        upward=!upward;
    }
    if (bit<count*8 || bit>count*8+7)
        return failure(Error::invalid_input,"Internal module reservation/codeword count mismatch");

    // Blocks differ in data length by at most one; the shorter blocks come first.
    const std::size_t short_length=count/params.count;
    const std::size_t long_blocks=count%params.count;
    const std::size_t short_blocks=params.count-long_blocks;
    const std::size_t short_data=short_length-params.parity;
    std::vector<std::vector<std::uint8_t>> blocks;
    blocks.reserve(params.count);
    for (std::size_t block=0; block<params.count; ++block)
        blocks.emplace_back(short_length+(block>=short_blocks ? 1:0),0);
    std::size_t source=0;
    for (std::size_t column=0; column<=short_data; ++column)
        for (std::size_t block=0; block<params.count; ++block)
            if (column < blocks[block].size()-params.parity)
                blocks[block][column]=words[source++];
    for (std::size_t column=0; column<params.parity; ++column)
        for (std::size_t block=0; block<params.count; ++block)
            blocks[block][blocks[block].size()-params.parity+column]=words[source++];
    if (source!=words.size()) return failure(Error::invalid_input,"Internal interleave length mismatch");
    std::vector<std::uint8_t> data;
    data.reserve(count-static_cast<std::size_t>(params.count)*params.parity);
    unsigned corrected=0;
    for (auto& block: blocks) {
        unsigned block_corrected=0;
        if (!detail::correct_rs(block,params.parity,block_corrected))
            return failure(Error::ecc,"Reed-Solomon correction failed");
        corrected+=block_corrected;
        data.insert(data.end(),block.begin(),block.end()-params.parity);
    }
    auto result=detail::parse_payload(data,version);
    if (!result) return result;
    if (strict_padding && result.symbol.noncanonical_padding)
        return failure(Error::payload,"Noncanonical extra zero padding byte rejected by strict padding option");
    result.symbol.version=version;
    result.symbol.mask=mask;
    result.symbol.ec_level=levels[level];
    result.symbol.format_distance=format_distance;
    result.symbol.corrected_codewords=corrected;
    result.symbol.rotation=grid.rotation*90;
    result.symbol.mirrored=grid.mirror;
    result.symbol.inverted=grid.invert;
    if (diagnostics) {
        if (result.symbol.noncanonical_padding)
            result.symbol.trace.push_back({"compatibility", "Accepted one extra zero byte after a byte-aligned terminator"});
        result.symbol.trace.push_back({"format", "BCH distance="+std::to_string(format_distance)});
        if (version>=7) result.symbol.trace.push_back({"version", "BCH distance="+std::to_string(version_distance)});
        result.symbol.trace.push_back({"extract", "codewords="+std::to_string(count)+
            "; nonzero remainder bits="+std::to_string(remainder_nonzero)});
        result.symbol.trace.push_back({"ecc", "blocks="+std::to_string(params.count)+
            "; parity per block="+std::to_string(params.parity)+
            "; corrected codewords="+std::to_string(corrected)});
    }
    return result;
}

int progress(Error error) {
    switch (error) {
    case Error::payload: case Error::unsupported: return 4;
    case Error::ecc: return 3;
    case Error::version: return 2;
    case Error::format: return 1;
    default: return 0;
    }
}
} // namespace

DecodeResult decode_grid(GridView grid, const Options& options) {
    // Validate lengths before any pointer arithmetic. Work is intrinsically
    // bounded by Model 2's 177x177 modules and at most 16 orientation/polarity tries.
    if (!grid.data || grid.size<21 || grid.size>177 || (grid.size-17)%4!=0)
        return failure(Error::invalid_input,"Model 2 grid dimension must be 21..177 and equal 17+4*version");
    if (grid.size>options.limits.max_dimension || grid.size*grid.size>options.limits.max_pixels)
        return failure(Error::resource_limit,"Grid dimension or module count exceeds configured limit");
    if (grid.stride<grid.size || grid.data_size<grid.size ||
        grid.size-1>(grid.data_size-grid.size)/grid.stride)
        return failure(Error::invalid_input,"Grid buffer/stride is shorter than its dimensions");
    for (std::size_t y=0; y<grid.size; ++y)
        for (std::size_t x=0; x<grid.size; ++x)
            if (grid.data[y*grid.stride+x]>1)
                return failure(Error::invalid_input,"Grid modules must be exactly zero or one");
    DecodeResult best=failure(Error::format,"No orientation produced decodable format information");
    std::size_t attempts=0;
    try {
        for (unsigned invert=0; invert<=(options.try_invert?1U:0U); ++invert)
            for (unsigned mirror=0; mirror<=(options.try_mirror?1U:0U); ++mirror)
                for (unsigned rotation=0; rotation<4; ++rotation) {
                    if (attempts>=options.limits.max_grid_attempts)
                        return failure(Error::resource_limit,"Grid orientation attempt limit reached");
                    ++attempts;
                    auto candidate=canonical_decode({grid,rotation,mirror!=0,invert!=0},options.diagnostics,options.strict_padding);
                    if (candidate) return candidate;
                    if (progress(candidate.error)>progress(best.error)) best=std::move(candidate);
                }
    } catch (const std::bad_alloc&) {
        return failure(Error::resource_limit,"Allocation failed within bounded grid decoding");
    }
    return best;
}
} // namespace specqr
