# Limits and measured image behavior

SpecQR Reader 0.1.1 is an experimental, bounded QR Model 2 reader. Micro QR, rMQR, automatic text transcoding, Structured Append reassembly, erasure hints, camera capture and general image codecs are outside its interface. Detection targets planar symbols; curved surfaces, dense clutter, damaged function patterns and poor sampling can defeat it. Close, overlapping or nested symbols may be suppressed by spatial deduplication.

There is no formal proof of the Reader's parsing, correction, image decoding, memory safety or full QR correctness. Independent tests do not establish normative conformance. No proof from earlier Ada/SPARK or Lean work is transferred to this C++ implementation. Full normative ISO clauses were not accessed.

## Measured image conditions

“Exact” below means the expected byte multiset, including symbol multiplicity, was returned. “Empty” means no symbol was returned. “Work flags” record incomplete search, including successes that recovered all expected bytes. These finite observations are not deployment recall or false-decode probabilities.

| Condition | Images | Exact | Empty | Wrong/extra | Work flags |
| --- | ---: | ---: | ---: | ---: | --- |
| Gaussian blur, radius 0.9 pixels at 5 pixels/module | 4 | 4 | 0 | 0 | 0 |
| New planar perspective warps | 20 | 20 | 0 | 0 | 3 successful searches |
| Small white occlusion patches or finder-crossing strips | 8 | 6 | 2 | 0 | 0 |
| Low contrast and selected illumination gradients | 12 | 11 | 1 | 0 | 0 |
| Nominal module sizes from 1 to 3 pixels | 12 | 8 | 4 | 0 | 0 |
| Older real photographs, now development regressions | 50 | 47 | 3 | 0 | 0 |

The blur row is from the main test suite. The next four rows are selected categories from a separate newer synthetic suite; the rows must not be combined into one representative population.

The four blur cases use versions 1, 3, 7 and 12 at error level H. They establish one tested mild Gaussian blur setting, not a maximum blur radius or motion-blur tolerance. Separate intensity-noise tests at sigma 12 and 35 on a 0–255 scale decoded four images each; noise is not dirt or missing modules.

The twenty perspective cases use versions 1, 2, 4, 7 and 10, rotations from -43 to 31 degrees, projective corner warps and an unclipped quiet zone. Generator corner offsets are coordinate displacements, not camera tilt angles or a percentage of physical distortion. Three exact results still flagged incomplete search.

For occlusion, six small interior white patches on a version-3 H symbol at 5 pixels/module decoded. Patch footprints ranged from 2×2 to 3×3 or 4×2/2×4 modules. Two white strips crossing the top-left finder returned empty. Location matters: these are artificial patches, not a real-dirt, scratched-print or arbitrary damaged-area guarantee. Background clutter tests paste an intact QR and quiet zone over the clutter and therefore do not test dirt on the QR itself.

All four uniformly lit contrast fixtures decoded, including dark/light intensities 60/80. One 60/80 case combined with a 0.35-to-1 horizontal brightness gradient returned empty; eleven of twelve combined contrast/illumination fixtures decoded. Some fixtures also include blur. These pixel values do not establish a universal contrast, lux or camera exposure threshold.

Both 1-pixel/module cases and both 1.25-pixel/module cases returned empty. The eight cases at 1.5, 1.75, 2 and 3 pixels/module decoded. Resampling, differing payloads and differing versions mean this does not establish a universal 1.5-pixel minimum.

The fifty older photographs came from the pinned ZXing corpus with upstream notices retained in the separate evidence collection. Their use in development means they are no longer held-out data. Two misses depict fabric; the third is a screen fixture rejected by the documented padding policy, not a proved failure from visual damage.

Four newer positive photographs from three BoofCV scene groups were frozen before evaluation and tested as gray and RGB, producing eight correlated raster evaluations. Every evaluation returned nonempty byte multisets agreeing with the independent decoder. Independent exact payload truth remains unknown, so exact-correct and wrong-byte counts are unknown. Both clipboard photos flagged incomplete search in both adapters, four of the eight evaluations. Six visually QR-free photos returned empty in both adapters; the coffee scene was work-limited in both. Visual inspection and finite empty results do not prove absence of every QR symbol or a population false-decode rate.

The complete newer synthetic suite returned 73 exact positives and seven empty positives out of 80, with zero observed wrong or extra outputs and three successful work-limited searches. Its sixteen synthetic negatives were all empty, two work-limited. Separately, the main suite's image subset returned 78 exact positives and 18 empty negatives, with fourteen positive and ten negative work flags. These suites overlap in intent and must remain separate measurements.

## Error correction

The algebraic unique-decoding radius is `floor(parity_codewords / 2)` wrong codewords **per block**. This property is not a formal proof that every implementation path is correct. Error-correction percentages must not be interpreted as a percentage of pixels or arbitrary obscured image area: one patch can damage finder patterns or many codewords across different blocks.

Outside the correction radius, Reed–Solomon can select a different valid codeword. Finite rejection controls cannot promise universal detection of excessive corruption, and the API accepts no erasure hints.

## Resources and hostile input

The API validates dimensions, strides, multiplication and last-row extents before accessing pixels. Model 2 grid size is capped at 177×177 modules. Image dimensions, pixel count, candidate retention, sampling attempts, output count and finite search loops are bounded even if callers request very large limits.

The default sampling budget is 2,048 attempts. The probe ceiling is `min(256000000, 4000000 + 65536 * attempt_cap)`; one in-flight pattern/sample can cross it before the next guard. The engineering audit bounds that overshoot at 36,718 charged probes, not a formal proof. Finder scanning, candidate clustering and geometric hypotheses have separate finite work; a sampling cap is not a total operation count or wall-clock deadline.

Raster working storage is approximately two bytes per input pixel plus `16 × (ceil(width/8)+1) × (ceil(height/8)+1)` bytes for local threshold tables. At the default pixel limit this is roughly 36 MiB before caller buffers, outputs, container overhead and runtime memory. There is no aggregate heap quota; segment metadata and output copies can substantially increase memory at high symbol caps. Allocation exhaustion can escape as `std::bad_alloc`, including while constructing an error result.

Five fixed large-negative controls returned empty in the cloud acceptance replay. Mean core times ranged from about 77 to 4,175 ms and maximum observed process RSS was 55,024 KiB; noise and finder clutter reached work limits. These are shared-host measurements for named controls, not portable performance bounds. RSS includes process memory and is not an isolated core-heap measurement. There is no cancellation or hard real-time API.

Applications processing untrusted images should set limits appropriate to their own latency and memory requirements and inspect partial-result status. The finite malformed-input smoke suite and sanitizers are useful checks, not exhaustive fuzz coverage or a security certification.

## Platforms and acceptance scope

The parent independently reconciled Mac and Windows native evidence and matched the nine runtime/header/CLI files. Windows evidence covers GCC x64 under Windows ARM64 emulation; native Windows ARM64 ABI is untested. Compiler/runtime combinations without recorded execution remain unverified even if represented by a CI configuration.

Earlier cloud downloads of native archives failed as transfers. The parent's subsequent successful archive inspection closed the native-evidence reconciliation gate; it did not cause those bytes to become locally available in that cloud executor. The user has approved MIT distribution under SpecQR; final parent review of the assembled package remains required before any repository creation or public push. No stronger production, conformance, photographic or platform claim follows from accepting this bounded experimental candidate.
