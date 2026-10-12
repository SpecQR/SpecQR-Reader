# SpecQR Reader 0.1.1

SpecQR Reader is a dependency-free C++17 reader for QR Model 2. It decodes caller-sampled module grids and searches grayscale or RGB images for planar symbols. It returns exact payload bytes and segment metadata, including ECI, FNC1 and Structured Append information.

This is an experimental source review candidate intended for `SpecQR/SpecQR-Reader`. Newly authored project code is licensed under MIT. This repository contains the reviewed experimental source. Hosted CI and fresh public-clone validation are being verified. It has finite input and work limits, independently checked fixtures, and native portability evidence; it does not claim full QR conformance, formal correctness, or reliable decoding of every photograph.

The library uses only the C++17 standard library. C++17 permits a portable image-processing core and ordinary native integration without an interpreter or codec dependency; callers must manage buffer lifetimes and account for allocation failure. The optional command-line adapter reads PNM images and text grids. PNG/JPEG loading, cameras, text conversion and Structured Append reassembly belong in application adapters. Reed–Solomon correction is an internal Reader component, not a separate ECC API.

## Build and test

With a C++17 compiler, Make and Python 3:

```sh
make -j2 all
make test
./build/consumer fixtures/grids/segno-v01-H-m0.grid
./build/specqr-reader grid fixtures/grids/segno-v01-H-m0.grid
```

`make test-core` runs the standalone C++ tests without Python. `make test-gate` runs the Python standard-library evaluator controls. `make test` runs both. Included fixture data is fixed; running these tests does not download or regenerate reference data. The optional `make test-reference` requires the separately retained full historical corpus and reference adapters, which this source package does not include.

With CMake 3.16 or newer:

```sh
cmake -S . -B out -DCMAKE_BUILD_TYPE=Release -DSPECQR_BUILD_TESTS=ON
cmake --build out --config Release --parallel
(cd out && ctest -C Release --output-on-failure)
cmake --install out --config Release --prefix "$PWD/install"
```

Tests are opt-in for CMake. `SPECQR_BUILD_TOOLS=OFF` and `SPECQR_BUILD_EXAMPLES=OFF` disable the optional command-line tool and example. The installed package is `SpecQRReader`; the imported target is `SpecQR::specqr_reader`:

```cmake
find_package(SpecQRReader 0.1 REQUIRED CONFIG)
add_executable(my_reader main.cpp)
target_link_libraries(my_reader PRIVATE SpecQR::specqr_reader)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix when configuring the consuming project. The public header is `<specqr/reader.hpp>`.

The cross-platform CI driver configures and builds CMake tests, runs the evaluator controls, installs the library, and builds a detached consumer against the relocated installation:

```sh
python3 tests/run_ci.py --work-dir /absolute/path/to/new-ci-work
```

Use the Python executable name available on your platform. The work directory must not exist yet and must be outside the source tree. A configured CI job is not evidence that a hosted job has run; current validation scope is recorded in [validation](docs/validation.md).

On a supported compiler/runtime, `make sanitize` runs the finite core suite under AddressSanitizer and UndefinedBehaviorSanitizer. Leak checking defaults to enabled outside Darwin and disabled on Darwin because of the reported Apple runtime limitation; override with `SANITIZER_DETECT_LEAKS=0` or `1` as appropriate. Sanitizer availability depends on the chosen toolchain.

## Use the Reader

`decode_grid()` accepts one byte per module: `0` is light and `1` is dark, with no quiet zone. Model 2 sizes are 21–177 modules in steps of four. `read_image()` accepts an interleaved, top-to-bottom, 8-bit grayscale or RGB buffer and returns a list of physical symbols. Input buffers remain caller-owned and are not retained after the call.

Payloads are byte vectors, not Unicode strings. Segment boundaries and ECI assignments are preserved. FNC1 alphanumeric `%` escapes remain literal bytes for the application to interpret. See the [API contract](docs/api.md) and [consumer example](examples/consumer.cpp).

Inspect an image result's `error`, `symbols` and `work_limit_reached` together. A successful result may have incomplete search; an allocation-related `resource_limit` result may retain valid partial symbols. Neither a successful result nor a false work-limit flag certifies that every symbol was found. These APIs are not `noexcept`: allocation exhaustion can propagate `std::bad_alloc`.

The command-line adapter prints binary-safe JSON:

```sh
./build/specqr-reader image input.pgm --trace
./build/specqr-reader image input.ppm --invert --repeat 3
```

Image input must be binary P5/P6 PNM with maximum sample value 255. The adapter supports `--trace`, `--invert`, `--strict-padding` and `--repeat N`. Grid files contain the decimal side length on the first line, followed by exactly that many rows of `0`/`1` characters. Exit status 0 means successful status; status 2 means rejection, resource exhaustion or an input error. Always inspect the JSON for partial symbols and work flags. `bytes_hex` is the payload representation; no decoded URL or command is executed.

## Scope and evidence

The source package includes deterministic unit, public-API, malformed-input smoke and harness controls. Broader acceptance used independent encoders/decoders and photographs retained separately with provenance and notices. The included suite is a reproducible subset of that evidence, not a replacement for the full comparison corpus.

Measured image results include 73 exact and seven empty outcomes among 80 newer synthetic positives, and 47 exact and three empty outcomes among 50 older development photographs. Those corpora produced no wrong or extra outputs in their finite checks. Some successful synthetic searches were work-limited. These counts are not general recall or false-decode rates; see [limitations and measured conditions](docs/limitations.md).

The parent acceptance review independently reconciled Mac and Windows evidence with all nine runtime/header/CLI source hashes. Windows validation used an x64 GCC build under Windows ARM64 emulation; native Windows ARM64 ABI remains untested. Earlier download failures in the cloud executor were transfer failures, not failing native decoder runs. They do not remain an acceptance gate after the parent's archive inspection. [Validation](docs/validation.md) records the scope and attribution.

The [MIT license](LICENSE) uses the established `Copyright (c) 2026 SpecQR contributors` attribution. It applies to newly authored project code and documentation. See [THIRD_PARTY.md](THIRD_PARTY.md) for the separately scoped notices and generated-data provenance.
