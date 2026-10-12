# Public API

Include `<specqr/reader.hpp>` and link `SpecQR::specqr_reader` through the installed CMake package, or link the built static library. Both entry points are synchronous. Input buffers remain caller-owned and valid for the duration of the call; returned vectors and strings own their storage.

## Sampled module grids

```cpp
specqr::GridView view{
    modules.data(), // one byte per module
    side,           // square side, 21 + 4*k, k in [0, 39]
    stride,         // bytes between row starts, at least side
    modules.size()  // accessible bytes from data
};
specqr::DecodeResult result = specqr::decode_grid(view);
if (result) {
    const auto& payload = result.symbol.bytes;
    // Consume payload as bytes; choose any text interpretation explicitly.
}
```

Modules must be exactly `0` (light) or `1` (dark). Do not include the quiet zone. The last row must fit within `data_size`; row padding is permitted and ignored. Null data, invalid dimensions, values outside 0/1, and inconsistent extents are rejected. The caller must truthfully describe accessible memory; the library cannot discover the size of a raw pointer's allocation.

`DecodeResult::operator bool()` is true exactly when `error == Error::none`. Read `symbol` only for a successful grid decode. The grid path tries rotations, optional mirroring, and optional inversion, then validates format/version information, corrects codewords and parses payloads. A failure error describes the furthest decoding stage reached, not an exhaustive diagnosis of every possible orientation.

## Grayscale and RGB images

```cpp
specqr::Options options;
options.limits.max_symbols = 8;
options.diagnostics = true;
specqr::ImageView view{
    pixels.data(), width, height,
    stride,        // bytes between row starts
    3,             // 1 = gray; 3 = interleaved R, G, B
    pixels.size()
};
specqr::ImageResult result = specqr::read_image(view, options);
for (const auto& symbol : result.symbols) {
    // Handle each recovered payload, even if result.error is resource_limit.
}
const bool search_was_limited = result.work_limit_reached;
```

Images use 8-bit samples, top-to-bottom rows, and a positive unsigned stride. Width and height must be nonzero. The row and last-row extents are checked for overflow and against `data_size` before access. There is no alpha, BGR, packed-bit, planar-channel or negative-stride input format; convert in an adapter. RGB is converted to integer luminance inside the core.

The detector searches for planar Model 2 symbols and deduplicates them by position. Equal payloads at separate tested positions remain separate symbols. Output order is discovery order; do not rely on a stable reading order or exhaustive scene coverage.

### Status and partial results

| Image result | Meaning |
| --- | --- |
| `error == none`, nonempty `symbols`, flag false | Symbols were recovered; no reported search truncation. Completeness is still not guaranteed. |
| `error == none`, nonempty `symbols`, flag true | Symbols were recovered, but a work, candidate, hypothesis or symbol limit truncated search. |
| `error == resource_limit`, nonempty `symbols` | A resource failure occurred after some symbols were recovered; preserve or discard those symbols according to application policy. |
| `error == resource_limit`, empty `symbols` | No symbols were returned before a resource/work limit or allocation failure. |
| `error == not_found`, empty `symbols` | This search found no decodable symbol; this does not prove the image contains no QR code. |

`ImageResult::operator bool()` only tests `error == none`; it does not encode completeness. The implementation can finish a successful sample after crossing a probe ceiling and return without a work flag if no later uncovered hypothesis reaches the next guard. A false flag is therefore not a strict probe-counter certificate either.

Allocation failures are converted to `resource_limit` where caught. Error construction and copying a partial result can themselves allocate and throw `std::bad_alloc`. Both public entry points are deliberately not `noexcept`; applications requiring an allocation-free failure path need a different integration design.

## Exact bytes and metadata

`Symbol.bytes` concatenates data segments in their original order. It does not transcode text, infer an encoding, normalize characters or merge Structured Append symbols. `Symbol.segments` preserves data and zero-byte control records; each has its mode, active ECI and bit offset/length spanning the segment's mode indicator and fields.

| Segment mode | Byte representation |
| --- | --- |
| Numeric | ASCII digits, including leading zeroes. |
| Alphanumeric | ASCII characters from the QR alphabet. FNC1 `%` and `%%` remain literal source bytes. |
| Byte | Unchanged byte values, including NUL and invalid UTF-8. |
| Kanji | Reconstructed two-byte Shift JIS values, with structural range checks. |
| Hanzi subset 1 | Reconstructed two-byte GB2312 values, with structural range checks. Other subsets return `unsupported`. |
| ECI, FNC1, Structured Append | Empty control records; associated metadata is retained. |

An ECI value of `-1` means unspecified, not a guessed encoding. Canonical ECI forms through assignment 999999 are parsed; unknown assignments remain metadata. Range checks do not establish that every Kanji/Hanzi character is assigned in a character-set registry. Mixed-mode concatenation need not represent one Unicode encoding.

`fnc1` is 0 when absent, 1 for first position and 2 for second position. `fnc1_application_indicator` preserves the raw eight-bit second-position value, or is -1 when absent. Applications interpreting GS1 data must explicitly handle FNC1 escape conventions. In particular, the Reader does not replace a single `%` with GS or collapse `%%`.

`structured_append` records presence, zero-based index, symbol count (1–16) and parity. Reassembly and cross-symbol consistency checks belong to the application.

Other `Symbol` fields record version, mask, error-correction level, corrected codeword count, format distance and the rotation/mirroring/inversion used by decoding. Rotation is expressed in multiples of 90 degrees for the sampled grid; it is not a camera angle. For image results, `corners` describes the sampled quadrilateral in image coordinates. It is not reordered to promise canonical QR orientation after grid rotation/mirroring. Grid-only results have no image geometry.

## Options and limits

| Option | Default | Effect |
| --- | --- | --- |
| `try_mirror` | true | Try mirrored grid orientations. |
| `try_invert` | false | Also try light-on-dark polarity. |
| `diagnostics` | false | Collect stage information in result/symbol trace vectors. |
| `strict_padding` | false | Reject the narrow reported extra-zero padding accommodation when enabled. |
| `limits.max_pixels` | 16,777,216 | Limit image pixels or sampled grid modules. |
| `limits.max_dimension` | 8,192 | Limit each image dimension or grid side. |
| `limits.max_candidates` | 96 | Limit retained finder candidates per image threshold pass. |
| `limits.max_symbols` | 32 | Limit returned image symbols; reaching the cap marks incomplete search. |
| `limits.max_grid_attempts` | 2,048 | Limit image sampling attempts, or grid orientation attempts for direct `decode_grid()`. |

Image sampling attempts can each invoke up to eight grid orientations; those are separate counters. Hard image ceilings apply even when callers raise limits: 67,108,864 pixels, dimension 32,768, 128 finder candidates per pass, 16,384 sampling attempts, and 1,024 output symbols. Lower limits can reduce recall. See [resource limitations](limitations.md#resources-and-hostile-input) before selecting limits for untrusted images.

Default padding handling accepts one specifically detected extra-zero variant observed from Segno 1.6.6 and sets `noncanonical_padding`. It still validates subsequent padding. `strict_padding=true` rejects that variant through either entry point. This is an explicit interoperability policy, not a claim about unavailable normative standard clauses.

Diagnostics are opt-in engineering information. Trace text and event layout are not a stable machine protocol; use result fields and `error_name()` for application status. Messages describe an individual run and should not be parsed as a conformance verdict.
