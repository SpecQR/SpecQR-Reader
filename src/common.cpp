#include "specqr/reader.hpp"
namespace specqr {
const char* error_name(Error e) {
    switch(e) {
    case Error::none: return "none";
    case Error::invalid_input: return "invalid_input";
    case Error::resource_limit: return "resource_limit";
    case Error::not_found: return "not_found";
    case Error::format: return "format";
    case Error::version: return "version";
    case Error::ecc: return "ecc";
    case Error::payload: return "payload";
    case Error::unsupported: return "unsupported";
    }
    return "unknown";
}
}
