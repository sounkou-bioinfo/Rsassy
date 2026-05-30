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
  mode = "single",
  match_region = FALSE
)
```

## Arguments

- pattern, text:

  Raw vectors, character vectors, or lists of raw vectors / character
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

  If `FALSE`, return local-minimum matches. If `TRUE`, return all end
  positions with score \<= `k`.

- threads:

  Number of worker threads to request for bulk searches.

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

## Examples

``` r
sassy_search("ACGT", "TTACGTAA", 0, alphabet = "dna", rc = FALSE)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          2        6             0           4    0      +    4=
```
