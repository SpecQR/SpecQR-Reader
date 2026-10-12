#include "internal.hpp"
#include <array>

namespace specqr { namespace detail {
namespace {
// QR uses GF(2^8), primitive polynomial x^8+x^4+x^3+x^2+1,
// and consecutive generator roots alpha^0 .. alpha^(parity-1).
struct Field {
    std::array<unsigned char, 510> exp{};
    std::array<unsigned char, 256> log{};
    Field() {
        unsigned x = 1;
        for (unsigned i=0; i<255; ++i) {
            exp[i]=static_cast<unsigned char>(x);
            log[x]=static_cast<unsigned char>(i);
            x <<= 1;
            if (x & 256U) x ^= 0x11dU;
        }
        for (unsigned i=255; i<510; ++i) exp[i]=exp[i-255];
    }
    unsigned mul(unsigned a, unsigned b) const {
        return a && b ? exp[unsigned(log[a])+unsigned(log[b])] : 0;
    }
    unsigned div(unsigned a, unsigned b) const {
        // Callers ensure that the divisor is nonzero.
        return a ? exp[unsigned(log[a])+255U-unsigned(log[b])] : 0;
    }
};
const Field& field() { static const Field f; return f; }
unsigned evaluate(const std::vector<std::uint8_t>& block, unsigned x) {
    const auto& f=field();
    unsigned result=0;
    for (unsigned b : block) result=f.mul(result,x)^b;
    return result;
}
}

bool correct_rs(std::vector<std::uint8_t>& block, unsigned parity, unsigned& corrected) {
    corrected=0;
    if (parity==0 || parity>30 || block.size()>255 || block.size()<=parity) return false;
    const auto& f=field();
    std::array<unsigned,30> syndromes{};
    bool clean=true;
    for (unsigned j=0; j<parity; ++j) {
        syndromes[j]=evaluate(block,f.exp[j]);
        clean=clean && syndromes[j]==0;
    }
    if (clean) return true;

    // Berlekamp--Massey builds the shortest syndrome recurrence. Coefficients
    // are in ascending order, so C(z)=product(1 + X_i z).
    std::array<unsigned,31> locator{}, previous{};
    locator[0]=previous[0]=1;
    unsigned degree=0, shift=1, last_discrepancy=1;
    for (unsigned n=0; n<parity; ++n) {
        unsigned discrepancy=syndromes[n];
        for (unsigned i=1; i<=degree; ++i)
            discrepancy ^= f.mul(locator[i],syndromes[n-i]);
        if (discrepancy==0) { ++shift; continue; }
        const auto saved=locator;
        const unsigned scale=f.div(discrepancy,last_discrepancy);
        for (unsigned i=0; i+shift<locator.size(); ++i)
            locator[i+shift] ^= f.mul(scale,previous[i]);
        if (2*degree<=n) {
            degree=n+1-degree;
            previous=saved;
            last_discrepancy=discrepancy;
            shift=1;
        } else ++shift;
    }
    if (degree==0 || 2*degree>parity) return false;

    // Chien search is confined to the actual shortened block positions.
    std::array<unsigned,15> positions{}, locations{};
    unsigned roots=0;
    for (unsigned p=0; p<block.size(); ++p) {
        const unsigned exponent=static_cast<unsigned>(block.size())-1-p;
        const unsigned inverse=f.exp[(255-exponent)%255];
        unsigned value=locator[degree];
        for (unsigned i=degree; i>0; --i) value=f.mul(value,inverse)^locator[i-1];
        if (value==0) {
            if (roots>=degree) return false;
            positions[roots]=p;
            locations[roots]=f.exp[exponent];
            ++roots;
        }
    }
    if (roots!=degree) return false;

    // Solve S_j = sum(e_i X_i^j). At most 15 errors keeps this small,
    // bounded Gaussian elimination clearer than convention-sensitive Forney
    // formulas. Every element and temporary is a GF(256) value.
    std::array<std::array<unsigned,16>,15> matrix{};
    for (unsigned col=0; col<degree; ++col) {
        unsigned power=1;
        for (unsigned row=0; row<degree; ++row) {
            matrix[row][col]=power;
            power=f.mul(power,locations[col]);
        }
    }
    for (unsigned row=0; row<degree; ++row) matrix[row][degree]=syndromes[row];
    for (unsigned col=0; col<degree; ++col) {
        unsigned pivot=col;
        while (pivot<degree && matrix[pivot][col]==0) ++pivot;
        if (pivot==degree) return false;
        if (pivot!=col) matrix[pivot].swap(matrix[col]);
        const unsigned divisor=matrix[col][col];
        for (unsigned k=col; k<=degree; ++k) matrix[col][k]=f.div(matrix[col][k],divisor);
        for (unsigned row=0; row<degree; ++row) {
            if (row==col) continue;
            const unsigned scale=matrix[row][col];
            for (unsigned k=col; k<=degree; ++k) matrix[row][k]^=f.mul(scale,matrix[col][k]);
        }
    }
    auto candidate=block;
    for (unsigned i=0; i<degree; ++i) {
        if (matrix[i][degree]==0) return false;
        candidate[positions[i]]^=static_cast<std::uint8_t>(matrix[i][degree]);
    }
    for (unsigned j=0; j<parity; ++j) if (evaluate(candidate,f.exp[j])!=0) return false;
    block.swap(candidate);
    corrected=degree;
    return true;
}
} }
