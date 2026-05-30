# Search with a reusable 'sassy' searcher

`pattern` and `text` must be lists of sequences. Each element must be a
raw vector or a non-missing character scalar. Every pattern is searched
against every text and the returned `pattern_idx` and `text_idx` columns
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
  pattern_id = NULL,
  text_id = NULL,
  match_region = FALSE,
  sam = FALSE
)
```

## Arguments

- searcher:

  A searcher created by
  [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md).

- pattern:

  List of raw vectors or non-missing character scalars.

- text:

  List of raw vectors or non-missing character scalars.

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

- pattern_id:

  Optional pattern identifiers. If supplied, must be a non-missing
  character vector with one entry per pattern and adds/replaces a
  `pattern_id` column. Names on `pattern` are not inspected.

- text_id:

  Optional text identifiers. If supplied, must be a non-missing
  character vector with one entry per text and adds/replaces a `text_id`
  column. Names on `text` are not inspected.

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
`cost`, `strand`, and `cigar`. If `pattern_id` or `text_id` are
supplied, mapped identifier columns are included. If requested, also
includes `match_region`. Rows are ordered by input text, then text
start/end coordinate, then pattern index.
