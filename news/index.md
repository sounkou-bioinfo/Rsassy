# Changelog

## Rsassy 0.2.1-0.1.0.9000

- Added a webR/WebAssembly package check in CI.
- Built webR packages with static Rust FFI symbols and `wasm_simd128`.
- Added native package-level SIMD backends:
  - x86_64: scalar, AVX2, AVX-512.
  - arm64/aarch64: scalar, NEON.
- Added one-shot backend selection:
  - automatic best-backend selection on first native call;
  - explicit
    [`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
    before first native call.
- Added
  [`sassy_features()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_features.md)
  fields for installed backends, selected backend, compiled features,
  and CPU/runtime capabilities.
- Added CRAN-style Rust vendoring with an offline Cargo vendor tarball.
- Added CRAN-safe default Cargo parallelism and local fast Makefile
  targets.
- Added vector and bulk search support for
  [`sassy_searcher_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher_search.md),
  [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md),
  and
  [`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md).
- Added bulk modes `"single"`, `"batch_texts"`, `"batch_patterns"`, and
  `"encoded_patterns"`/`"v2"`.
- Added native Rust/Rayon threading for bulk search; wasm32 remains
  serial.
- Added `match_region = TRUE`; reverse-strand regions are returned in
  pattern direction.
- Added class `sassy_matches` and
  [`print.sassy_matches()`](https://sounkou-bioinfo.github.io/Rsassy/reference/print.sassy_matches.md).
- Added optional ANSI coloring for printed match regions.
- Added SAM-compatible reverse-strand formatting with `sam = TRUE` and
  [`sassy_as_sam()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_as_sam.md).
- Added
  [`sassy_crispr()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_crispr.md)
  for in-memory CRISPR guide target searches.
- Added R connection input through
  [`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md).
- Added result columns `pattern_idx`, `text_idx`, `text_start`,
  `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and
  `cigar`.
- Added the R API functions
  [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md),
  [`sassy_searcher_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher_search.md),
  and
  [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md).
- Added configure checks for Cargo and `rustc >= 1.91.0`.
- Added bundled Rust crate authorship and license notes.
