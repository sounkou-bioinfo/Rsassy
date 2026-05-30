# Search with a reusable 'sassy' searcher

`pattern` and `text` may be single sequences or vectors/lists of
sequences. When vectors are supplied, every pattern is searched against
every text and the returned `pattern_idx` and `text_idx` columns
identify the 0-based input indices. Use `threads > 1` for larger
batches.

## Usage

``` r
sassy_searcher_search(
  searcher,
  pattern,
  text,
  k,
  all = FALSE,
  threads = 1L,
  strategy = "pairwise",
  match_region = FALSE,
  sam = FALSE
)
```

## Arguments

- searcher:

  A searcher created by
  [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md).

- pattern:

  Raw vector, character vector, or list of raw vectors / character
  scalars.

- text:

  Raw vector, character vector, or list of raw vectors / character
  scalars.

- k:

  Maximum edit distance.

- all:

  If `FALSE`, return the usual local-minimum matches. If `TRUE`, return
  every end position with score \<= `k`; this can include overlapping
  and nested candidate alignments and requires `strategy = "pairwise"`.

- threads:

  Number of worker threads to request for bulk searches.

- strategy:

  Search strategy. `"pairwise"` searches each pattern/text pair
  independently and is the general default. `"batch_texts"` uses one
  text per SIMD lane. `"batch_patterns"` and `"encoded_patterns"` (alias
  `"v2"`) use Sassy's multi-pattern encoding, which in `sassy` 0.2.1 is
  implemented for `alphabet = "iupac"` and equal byte-length patterns.

- match_region:

  If `TRUE`, include a `match_region` column. Reverse-strand regions are
  reverse-complemented so the region and CIGAR are in the input pattern
  direction.

- sam:

  If `TRUE`, format reverse-strand `match_region` and `cigar` in the
  text direction used by SAM and by the upstream `sassy --sam` output.

## Value

A data frame with 0-based indices and coordinates: `pattern_idx`,
`text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`,
`cost`, `strand`, and `cigar`. If requested, also includes
`match_region`. Rows are ordered by input text, then text start/end
coordinate, then pattern index.
