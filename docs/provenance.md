# Tables and synthetic test provenance

The source package builds and runs its included C++ tests from fixed checked-in data. External encoders, decoder comparisons, image codecs and their distributions are optional development references and are not needed by the product or the included core tests. Run the build/test commands in the [README](../README.md); `make test-core` exercises the checked-in RS/FNC1 vectors and padding grid without an encoder installation.

The SHA-256 values below identify the unchanged generated data and its recorded sources. These are reproducibility/provenance statements, not proofs of QR conformance or claims that the tests cover every supported input. Regenerating fixtures is a separate optional activity; do it in a disposable checkout and compare hashes before replacing any file.

## Pinned external generators

| Generator | Exact release/artifact | SHA-256 |
| --- | --- | --- |
| [python-qrcode](https://github.com/lincolnloop/python-qrcode) | `qrcode-8.2-py3-none-any.whl` | `16e64e0716c14960108e85d853062c9e8bba5ca8252c0b4d0231b9df4060ff4f` |
| [Segno](https://github.com/heuer/segno/) | `segno-1.6.6-py3-none-any.whl` | `28c7d081ed0cf935e0411293a465efd4d500704072cdb039778a2ab8736190c7` |
| [ZXing Java core](https://github.com/zxing/zxing/tree/f0f1a94439db0300ff0c4618f4e0a41aa630aeac) | `core-3.5.3.jar`, commit `f0f1a94439db0300ff0c4618f4e0a41aa630aeac` | `8d8064c1636fdaef7189dd9055c7d59950a8940a12f2293956446ec3c109fd82` |
| ZXing Java core sources | `core-3.5.3-sources.jar`, same commit | `f983454400d73652ad5236413ba7eba48072e042982a84b10e1468506f4b28ae` |
| [libqrencode](https://github.com/fukuchi/libqrencode/tree/715e29fd4cd71b6e452ae0f4e36d917b43122ce8) | 4.1.1 source archive at commit `715e29fd4cd71b6e452ae0f4e36d917b43122ce8` | `7777a49d6b838347ccc35dadd78e47bcf3962536da1b047ccd06a432cf796db9` |

The exact source-provenance notices included with this package are `third_party/notices/qrcode-8.2-LICENSE.txt` (SHA-256 `40dfb903c94ee3f789500131311186548ecba5cefd3557623d7700e2522ab994`) and `third_party/notices/segno-1.6.6-LICENSE.txt` (SHA-256 `de6c85fccf5d52902aa13dfe2dc6d2a2a106fc3419ed438f3460f0d4b76a6935`). Their scope and the MIT license for newly authored project code are explained in [THIRD_PARTY.md](../THIRD_PARTY.md).

## Factual parameter tables

`src/qr_tables.hpp`, SHA-256 `032dcd00ba98aa8e4dadfd214183b85dfe11742bbbc27a2b75f95dfb3dd15df7`, contains 160 version/error-level block count/parity pairs, 40 total-codeword values and 40 zero-terminated alignment rows. The historical development tool `tools/generate_qr_tables.py` (SHA-256 `be3e74081c169272cc41db296921551b6cd65eeea5af8206e017a059b1472c7c`) extracts the numbers from qrcode's `RS_BLOCK_TABLE` and `PATTERN_POSITION_TABLE`, asserts equality with Segno's `ECC` and `ALIGNMENT_POS`, and writes C++ arrays. It is a formatting/cross-check wrapper, not a decoder dependency. Historical tool paths identify their development roles; an absent optional tool is not needed to build this distribution.

The publication-preparation audit independently parsed the literal data in both pinned wheels, without executing their algorithms, and matched every included table value. The upstream comments identify ISO/IEC 18004:2015 table data and Annex E; this is indirect source evidence. No normative standard clauses are reproduced or claimed to have been read.

## Fixed RS blocks

`tests/rs_vectors.inc` has SHA-256 `7c3f084e168410771bbb7629128c242977ca68175ee2872881700172f9196c7c`. Its 96 blocks cover each parity count 7 through 30 and four lengths: `parity + 1`, `parity + 17`, 128 and 255. Payload byte `i` is `(i * 37 + parity * 19 + length) & 255`; qrcode 8.2's `BitBuffer`, `RSBlock` and `create_bytes` produce the fixed independent codeword output. The test verifies exact restoration of these blocks after deterministic corruptions and separately measures selected beyond-radius outcomes.

The included original wrapper `tests/generate_rs_vectors.py` is optional. It checks the installed qrcode version is 8.2 and rewrites the `.inc` file when invoked. Its historical `PYTHONPATH=references/python` example refers to the old development layout, which is not bundled. With the exact pinned qrcode wheel independently supplied in an isolated Python environment, run `python3 -B tests/generate_rs_vectors.py` from a disposable checkout and compare the resulting hash above. The package neither installs nor downloads that reference automatically.

## Single-grid and embedded image fixtures

| Fixture | Generation and exact expected bytes | SHA-256 of original text grid |
| --- | --- | --- |
| `fixtures/grids/segno-v01-H-m0.grid` | Segno 1.6.6; Model 2 version 1, H, mask 0, byte mode, ISO-8859-1 input, `boost_error=False`; raw bytes `c879d793d7`. Tests the explicitly flagged extra-zero padding accommodation and strict rejection. | `97662311a655b59f54d6511cabec9cd7b5531cb6e520ca2706a847c3db867f44` |
| Matrix embedded in `tests/image_boundary_tests.cpp` | qrcode 8.2; version 1, L, mask 5, byte mode with `optimize=0`, no border, `fit=False`; raw bytes `00ff014c0d`. The test renders its own grayscale/RGB canvases, rotations, reflections and separated duplicates. | `47e4a6b23c98c2efc97beb618d6d5217e8869a3149f14304c222c846d4ad2670` |

The original deterministic development generator was `tools/generate_fixtures.py`, SHA-256 `1e4180695de305e649cda91e64377d7c0d98f534cd9cdf13d81baee78a1d0274`. The Segno case came from its seeded byte-input sequence (`0x53515231`); its exact input above is sufficient to specify the selected case. This broad historical image/corpus generator is external to the source package. The text-grid hash serialization is decimal side, newline, one `0`/`1` row per line, and a final newline. The preparation audit matched both matrices to those original manifest hashes.

## FNC1 vectors

`tests/fnc1_vectors.inc`, SHA-256 `3618541270b861269dae65bca54c94b98802ca547004709100dfe8a7db563204`, contains 12 positive symbols and two malformed controls. `tests/fnc1_public_tests.cpp` exercises each as sampled modules, grayscale pixels and strided RGB pixels. Expected bytes and segment/control metadata are embedded, so test execution requires no Java, external C library or photo files.

Five FIRST-position symbols were produced by unmodified ZXing Java 3.5.3 `Encoder.encode`, using `GS1_FORMAT=true`, error level Q, mask 3 and requested version 4. The mixed cases enable `QR_COMPACT`; that path chooses the actual version recorded in each vector. An original wrapper observes the compact encoder's segment plan through reflection; it does not write segment bits, ECC or modules.

Seven SECOND-position symbols use unmodified libqrencode 4.1.1 `QRinput_setFNC1Second`, `QRinput_append` and `QRcode_encodeInput`, with version 4, error level Q, explicit typed source chunks, and its normal mask selection. The application indicators are respectively 42, 9, 165, 99, 197, 222 and 0 in table order below. The negative controls use public append operations to encode misplaced or duplicate SECOND headers with valid external ECC/matrix construction.

| Case | Expected result | Expected raw Reader bytes (hex) | Original text-grid SHA-256 |
| --- | --- | --- | --- |
| `fnc1-first-alpha-gs` | valid | `41424325313233` | `2938b1387b21a347d6938d362469ca00a47b0b583dd7e4b80175311533cfb10b` |
| `fnc1-first-alpha-percent` | valid | `52415445252535302525` | `00de5f96102bb181b6721d5e15a4fcb6bbb89eb298d4dffe144b3f91bf244121` |
| `fnc1-first-byte-percent-gs` | valid | `726177251d25257461696c` | `81b557ffc6cc61e4c84e18c624863d346330dc283be29c05449b9006caa3710b` |
| `fnc1-first-mixed` | valid | `4142253132333435363738393031323334353637383930252543441d787925` | `5d895b9b6b303b48334ee265ebe296dca1dbc9fa06e1cd33da5a571d6218dd85` |
| `fnc1-first-mixed-boundary` | valid | `41414141414141414141253132333435363738393031323334353637383930313233343536373839302561616161616161616161` | `4bfcb54580dc2313dcfffbce05b5dccee560e90309f298775fee873bb820e345` |
| `fnc1-second-alpha-gs` | valid | `41424325313233` | `b409a8ebee249de914a11bddaa1e4e5ecd0c671344c1f8cab773ab3b72b3e7b4` |
| `fnc1-second-alpha-percent` | valid | `52415445252535302525` | `d1fcef9d8059bf8737d8009b3dcc9fb9e14d7319729ef7efa2753583fd5bacd6` |
| `fnc1-second-byte-percent-gs` | valid | `726177251d25257461696c` | `5f10b85be15cbe856bf09d38cd0de542e75812a352bedf6a6fdb99ba37cb6a58` |
| `fnc1-second-mixed` | valid | `4125251d00ff2542` | `37e34cd0dfd2d76a483d6a72fe85f458acfbb20f198e2fe57a9b938ebef78465` |
| `fnc1-second-split-percent` | valid | `41252542` | `e74fa2147ff414bfbfbccc4d01d1cd631fe9dd62c3f4a615e814ae7751cb20d1` |
| `fnc1-second-double-percent-boundary` | valid | `41252525252542` | `43824583941af32261cf143143604313890613c098a04cc9a3e54a657d765d0f` |
| `fnc1-second-numeric-and-gs` | valid | `313233343536373839303132333435363738393025251d25` | `ef656a7bb49d10766dc103dabde8135479a5445f3944e1737942d75e91643b8f` |
| `fnc1-malformed-late-header` | reject | no symbol | `35c7ea6a2715f905dde9428077fe6a6c42a42aacc4388561943ecdcb71dded7f` |
| `fnc1-malformed-duplicate-header` | reject | no symbol | `b34489bf7c2ac36c2b914f3e3cdcc06dbf81d0c3d2e102858209af405242e710` |

The original development generator/wrappers were `tools/generate_fnc1_fixtures.py` (SHA-256 `630be88519d0853ef3d0cb9197a9862100b777154e0159abe1f497f7fee2981c`), `tools/Fnc1JavaEncoder.java` (`c58291829574789ae9295ddd853b931aa902c826a14aec96f6ae36353577e028`) and `tools/fnc1_libqrencode.c` (`a2422d2883f5d4678072e9aec216e4a1c1633dbca468ff669040991ff55d42f6`). These paths identify separately retained development tools, not prerequisites installed by this package. They verify the external artifacts, invoke the independent encoders, record segment plans and emit `.grid`, PGM and `.inc` forms. The preparation audit reconstructed the `.inc` file byte-for-byte from all preserved matrices and plans.

Reader expectations preserve literal `%` bytes and the raw SECOND indicator as metadata. ZXing-C++ comparison expectations were separately defined for that decoder's interpreted percent escapes and textual SECOND prefix; the evaluator did not normalize actual Reader output. No GS1 application-message validity, independent photographic FNC1 coverage, exhaustive input coverage or formal proof follows from these selected synthetic tests.
