# Search approximate matches with 'sassy'

Convenience wrapper that creates a searcher, searches, and returns a
`sassy_matches` data frame. Coordinates are 0-based and half-open.

## Usage

``` r
sassy_search(
  pattern,
  text,
  k,
  alphabet = "dna",
  rc = TRUE,
  alpha = NULL,
  all = FALSE,
  threads = 1L,
  strategy = "pairwise",
  match_region = FALSE,
  sam = FALSE
)
```

## Arguments

- pattern:

  Raw vector, character vector, or list of raw vectors / character
  scalars.

- text:

  Raw vector, character vector, or list of raw vectors / character
  scalars.

- k:

  Maximum edit distance.

- alphabet:

  Alphabet profile. One of `"dna"`, `"iupac"`, or `"ascii"`.

- rc:

  If `TRUE`, search reverse-complement strand as well where supported.

- alpha:

  Optional IUPAC overhang cost in `[0, 1]`. Use `NULL` to disable.

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

## Examples

``` r
sassy_search("ACGT", "TTACGTAA", 0, alphabet = "dna", rc = FALSE)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          2        6             0           4    0      +    4=
```
