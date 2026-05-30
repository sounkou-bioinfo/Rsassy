# Search with a reusable 'sassy' searcher

`pattern` and `text` may be single sequences or vectors/lists of
sequences. When vectors are supplied, every pattern is searched against
every text and the returned `pattern_idx` and `text_idx` columns
identify the 0-based input indices. Native builds can use Rayon threads
for bulk search when `threads > 1`; the current Rsassy wasm32/webR build
path keeps the same API but runs the bulk loop serially until threaded
webR execution is explicitly enabled and validated.

## Usage

``` r
sassy_searcher_search(
  searcher,
  pattern,
  text,
  k,
  all = FALSE,
  threads = 1L,
  mode = "single",
  match_region = FALSE
)
```

## Arguments

- searcher:

  A searcher created by
  [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md).

- pattern, text:

  Raw vectors, character vectors, or lists of raw vectors / character
  scalars.

- k:

  Maximum edit distance.

- all:

  If `FALSE`, return local-minimum matches. If `TRUE`, return all end
  positions with score \<= `k`.

- threads:

  Number of Rust/Rayon worker threads for native bulk searches. The
  current Rsassy wasm32/webR build path runs serially.

- mode:

  Bulk search mode. `"single"` searches each pair independently;
  `"batch_texts"` uses one text per SIMD lane; `"batch_patterns"` uses
  one pattern per SIMD lane and currently requires `alphabet = "iupac"`
  plus equal pattern lengths; `"encoded_patterns"` (alias `"v2"`) uses
  Sassy's encoded-pattern path and currently also requires
  `alphabet = "iupac"` plus equal pattern lengths.

- match_region:

  If `TRUE`, include a `match_region` column. Reverse-strand regions are
  reverse-complemented so the region and CIGAR are in the input pattern
  direction.

## Value

A data frame with 0-based indices and coordinates: `pattern_idx`,
`text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`,
`cost`, `strand`, and `cigar`. If requested, also includes
`match_region`.
