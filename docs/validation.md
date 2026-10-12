# Validation scope

This source package provides a reproducible offline subset of the Reader acceptance tests. It excludes the full reference distributions, photo corpora, native archives, compiled binaries and private acceptance records. Fixed included inputs have file-level provenance described in [THIRD_PARTY.md](../THIRD_PARTY.md); excluded photographs remain associated with their upstream notices in the separate evidence collection.

Test counts below describe different, sometimes overlapping controls. They must not be added into one independent population. Successful runs establish only their stated scope. No row establishes a formal correctness proof or full normative QR conformance.

## Tests included in the source package

Run `make test` for the C++ core tests and Python standard-library evaluator gate, or `make test-core` for C++ only. CMake's `SPECQR_BUILD_TESTS=ON` enables the six standalone C++ tests. `tests/run_ci.py` additionally checks the gate and a detached installed consumer; see the README for commands. Test assertions remain enabled in the CMake test targets, including Release builds.

| Included test | Purpose |
| --- | --- |
| `api_tests` | Public input validation and basic API behavior. |
| `payload_rs_tests` | Fixed independently encoded RS vectors, deterministic correction controls, payload/metadata boundaries and malformed parser/RS inputs. |
| `fuzz_smoke` | Fixed seed; 20,000 random public grids and 1,000 small random images by default. A finite smoke run, not a coverage-guided campaign. |
| `image_boundary_tests` | Image extent, stride, overflow, limits and boundary behavior. |
| `grid_padding_tests` | One included generated grid, default padding compatibility and strict rejection behavior. |
| `fnc1_public_tests` | Fixed independently encoded FIRST/SECOND FNC1 inputs through the public grid API. |
| `evaluator_gate_tests.py` | Eighteen mocked adapter and failure-gate controls; harness behavior, not QR algorithm coverage. |

`tests/rs_vectors.inc` is fixed output from qrcode 8.2. Its optional regeneration script requires that exact independently installed reference; normal tests use the committed vectors and need no reference encoder. Other fixed inputs also retain their generating/reference versions in the provenance documentation. Byte-for-byte fixture regeneration and independent decoder comparisons are separate activities from running these offline tests.

`make test-reference` is optional and requires the full separately retained comparison corpus and reference adapters. Its results must not be inferred from passing the included subset. `make sanitize` exercises the finite core suite under supported address/undefined-behavior sanitizer runtimes; leak detection support varies by platform.

The CI workflow and its command driver are part of this candidate. Local command validation and historical native evidence are reported separately; configuration alone does not mean that new hosted CI jobs have executed.

## Local publication-preparation validation

The clean source package was built on Linux x86_64 with GNU C++ 14.2.0, Python 3.12.14 and the previously pinned CMake/CTest 3.31.6. The Make `all test` path passed; CMake passed all six assertion-enabled CTests and all eighteen evaluator controls. An installed static library was moved to a new prefix, then a detached consumer compiled using only its installed public header and library. It verified exact grid bytes and metadata, strict-padding rejection, blank rejection, and exact bytes from a locally rendered grayscale image.

The Linux `make sanitize` command passed the same finite core suite with ASan, UBSan and leak checking enabled, including 54,639 payload/RS checks, 20,000 public random grids and 1,000 small random images, 158 image-boundary checks and 744 FNC1 checks. The public malformed-input run returned no decoded outputs; sanitizer stderr was empty. Eight mocked sanitizer-option portability controls also passed. These reruns overlap the historical tests and are not additional independent populations or a coverage-guided campaign.

The first CI-driver execution used CTest's newer `--test-dir` option. The final driver runs CTest with the build directory as its working directory to preserve the documented CMake 3.16 interface; both corrected invocations passed against unchanged binaries. The original run and corrected-command records are retained separately. CMake 3.16 itself and the newly configured hosted macOS/Windows jobs were not executed in this preparation pass. The workflow pins [actions/checkout v4.2.2](https://github.com/actions/checkout/commit/11bd71901bbe5b1630ceea73d27597364c9af683), grants read-only repository-content permission and disables persistent checkout credentials.

## Broader acceptance evidence retained separately

The accepted image implementation's existing checks include 1,535 product/reference fixture gates, 54,639 payload/RS checks, 744 public FNC1 checks, 158 image-boundary checks, budget/multiplicity/diagnostic controls and a detached public-header/static-library consumer. Linux AddressSanitizer and UndefinedBehaviorSanitizer runs included leak checking. A finite public malformed-input run used 20,000 grids and 1,000 small images with no decoded outputs. A coverage-guided campaign was not executed and is not included in these claims.

The October 12 acceptance follow-up added:

| Check | Outcome | Interpretation |
| --- | --- | --- |
| Capacity/BCH/orientation | 1,315/1,315 passed | Current core and deterministic inputs from a pinned independent encoder; includes all versions in near-capacity cases. |
| Unequal-block public corruption | 40/40 passed | Eight block configurations, five patterns each; exact bytes and correction counts through `decode_grid()`. |
| Allocation controls | 184/184 matched documented behavior | Four baselines and 180 ordinary allocation injections: 67 resource errors, 21 eventual successes, 92 escaped `std::bad_alloc` outcomes. |
| Corrupted-image fallback | Twelve Reader and three reference calls returned empty | Three fixed development corruptions; recover-or-reject controls, not guaranteed QR-free images. |
| Large negatives | Five final outputs empty | Fifteen core invocations via repeat-three; two final searches reported work limits. |
| Evaluator portability | Eighteen controls passed | One path-comparison fix; all 46 assertion-call expressions unchanged. |

Twenty-three allocation resource errors retained exact partial symbols with the work-limit flag. The allocation controls were release-build tests of ordinary allocations; they do not test every allocator path and are not a new sanitizer or coverage-guided run. An initial checker incorrectly required all errors to have empty symbols; its partial record and the corrected checker are preserved separately. No wrong payload, unexpected exception type or tracked ordinary-allocation leak was found in the final stated scope.

The original capacity report described an older source revision. The 1,315-case acceptance rerun explicitly binds the current core; historical evidence has not been silently promoted to the newer source. Corruption tests cover representative block boundaries and correction patterns, not every combination of version, mask and damage.

Image measurements and their confidence limits are summarized in [limitations](limitations.md). The newer positive photo comparisons have unknown independent payload truth. All fifty older photos are now development regressions, not new holdouts.

## Independent parent acceptance and native evidence

The cloud executor's initial native archive downloads and permitted retries failed at transfer. It recorded the failure and attributed native results to the parent rather than claiming local inspection. The parent subsequently obtained and independently verified both native archives, matched all nine runtime/header/CLI source hashes, confirmed the exact one-line Windows harness patch and all 46 unchanged assertions, reran eighteen Linux harness controls and rebuilt the public grid driver to pass all forty corruption cases.

The parent also checked the 714 listed archive payload entries, the raw 1,315 capacity records and 184 allocation outcomes. The acceptance source manifest has **38 entries**, including additional documentation; it must not be confused with the earlier 35-entry native/source manifest scope. This source-only publication-preparation package has its own explicit file inventory and excludes private evidence; it is not the same archive.

Mac evidence validates the improved image candidate within the supplied native report's scope. Windows evidence covers a GCC x64 build running under Windows ARM64 emulation: six assertion-enabled CTests, eighteen evaluator controls, 1,487 main vectors and an installed independent consumer. Fourteen successful partial searches retained the exact expected byte multisets and work flags. Native Windows ARM64 ABI is untested. The compact native archives do not contain executable bytes for independent binary-byte comparison.

The native-evidence reconciliation gate is now closed by the parent's inspection. This does not imply that those archives were downloaded into the cloud executor, or that unsupported native/compiler/sanitizer lanes were tested. No new blocking decoder defect was found for the bounded experimental scope. The runtime/header/CLI source remains unchanged; publication preparation changes packaging, documentation and development support. Product version remains 0.1.1.

## Feature-level status

| Feature | Implemented and exercised | Formal proof | Remaining limits |
| --- | --- | --- | --- |
| Model 2 grids, versions 1–40 | Independent encoders, capacity and malformed controls | None | Full normative conformance and every malformed input unverified. |
| RS correction/deinterleaving | Independent blocks and public unequal-block corruption controls | None | No erasure API or universal detection beyond correction radius. |
| Bytes, segments, ECI, FNC1, Structured Append metadata | Fixed independent FNC1 and bounded parser tests | None | No transcoding, registry validation or append reassembly. |
| Gray/RGB planar image detection | Synthetic categories, development photos, new scene comparisons | None | General recall, curved surfaces and new-photo exact truth unverified. |
| Multiple physical symbols | Multiplicity and duplicate-payload controls | None | Close/overlapping/nested arrangements and complete discovery unverified. |
| Work and input limits | Extent checks, finite-loop audits, allocation and negative controls | None | No heap quota, hard deadline, cancellation or `noexcept`. |
| Diagnostics | On/off result invariance and allocation controls | None | Arbitrary memory exhaustion and stable trace schema not promised. |
| Portable consumer interface | Cloud and reconciled native consumer evidence | None | Unrecorded platforms, including Windows ARM64 ABI, unverified. |

The user has approved distribution under SpecQR using MIT. Final parent review of the assembled package remains a gate before repository creation or a public push; no repository, tag, release or public upload has been performed during preparation.
