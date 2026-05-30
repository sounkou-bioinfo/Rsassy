# Rsassy 0.2.1-0.1.0.9000

* Added a webR/WebAssembly package check in CI.
* Built webR packages with static Rust FFI symbols and `wasm_simd128`.
* Added native package-level SIMD backends:
  * x86_64: scalar, AVX2, AVX-512.
  * arm64/aarch64: scalar, NEON.
* Added one-shot backend selection:
  * automatic best-backend selection on first native call;
  * explicit `sassy_set_backend()` before first native call.
* Added `sassy_features()` fields for installed backends, selected backend,
  compiled features, and CPU/runtime capabilities.
* Added CRAN-style Rust vendoring with an offline Cargo vendor tarball.
* Added CRAN-safe default Cargo parallelism and local fast Makefile targets.
* Added list-based bulk search support for `sassy_searcher_search()` and
  `sassy_search()`: `pattern` and `text` are now lists of raw vectors or
  character scalars, so each raw vector is one byte sequence and callers can mix
  raw, character, and ALTREP-backed string elements.
* Added bulk search strategies `"pairwise"`, `"batch_texts"`,
  `"batch_patterns"`, and `"encoded_patterns"`/`"v2"`.
* Added native Rust/Rayon threading for bulk search; wasm32 remains serial.
* Improved threaded `strategy = "pairwise"` scheduling to split Cartesian
  pattern/text tasks, which helps one-pattern-many-text workloads.
* Ordered returned matches by input text, then start/end coordinate, then
  pattern index, matching record-oriented CLI expectations.
* Added `match_region = TRUE`; reverse-strand regions are returned in pattern
  direction.
* Added class `sassy_matches` and `print.sassy_matches()`.
* Added optional ANSI coloring for printed match regions.
* Added SAM-compatible reverse-strand formatting with `sam = TRUE` and
  `sassy_as_sam()`.
* Added `pattern_id` and `text_id` mapping in the C data-frame construction
  path.
* Added native C/Rust CRISPR search support for in-memory guide/target lists.
* Removed the raw R connection stream API before release; FASTA/FASTQ input will
  use an explicit record-oriented file API rather than stream-relative text
  coordinates.
* Added result columns `pattern_idx`, `text_idx`, `text_start`, `text_end`,
  `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`.
* Added the R API functions `sassy_searcher()`, `sassy_searcher_search()`,
  and `sassy_search()`.
* Added configure checks for Cargo and `rustc >= 1.91.0`.
* Added bundled Rust crate authorship and license notes.
