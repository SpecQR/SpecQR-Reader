# Third-party provenance and notices

Newly authored SpecQR Reader code and documentation use the [MIT license](LICENSE), with the established `Copyright (c) 2026 SpecQR contributors` attribution. The license text matches [SpecQR at the pinned reference commit](https://github.com/SpecQR/SpecQR/blob/16efc6c0a8e397c9df3d051d20fce6c1eebdfad7/LICENSE). The notices below retain their respective upstream scope; the project MIT license does not replace them.

The C++17 Reader uses the standard library. No third-party encoder, decoder, file codec or camera implementation is compiled or linked into the product. The source package includes original implementation/test code, factual QR parameter tables, and fixed synthetic test inputs. Its normal tests run from the checked-in `.inc` and `.grid` files without installing an external encoder or decoder.

`src/qr_tables.hpp` contains numerical Model 2 block parameters, total codeword counts and alignment coordinates extracted from qrcode 8.2 and cross-checked with Segno 1.6.6. The two exact upstream license files are retained as source-provenance notices for that extraction/cross-check:

- [qrcode 8.2 notice](third_party/notices/qrcode-8.2-LICENSE.txt): BSD-3-Clause text and inherited MIT attribution as distributed in the pinned wheel.
- [Segno 1.6.6 notice](third_party/notices/segno-1.6.6-LICENSE.txt): BSD-3-Clause text as distributed in the pinned wheel.

Synthetic fixture generation also used qrcode 8.2, Segno 1.6.6, ZXing Java core 3.5.3 (Apache-2.0) and libqrencode 4.1.1 (LGPL-2.1-or-later). The included fixture matrices and RS blocks encode locally specified test bytes. They do not contain those projects' implementation source or binaries. Exact tool versions, commits, artifact hashes, generation paths and fixture expectations are documented in [provenance](docs/provenance.md).

The source package excludes third-party wheels/JARs/source distributions, comparison-decoder binaries, photographs, decoded photographic payloads and private evaluation archives. Historical ZXing photo replay inputs have their own retained upstream Apache-2.0 license, NOTICE and source/REUSE records in the separate validation archive. Their exclusion here is a package-scope choice, not a statement that they are unlicensed. Optional reference tooling or photo corpora must be obtained and managed separately with their applicable notices.

No normative standard text is redistributed. The table sources identify earlier ISO/IEC 18004 table/annex data; this is indirect provenance. The implementation did not obtain the full ISO/IEC 18004:2024 normative clauses and does not claim complete standard conformance or formal correctness.
