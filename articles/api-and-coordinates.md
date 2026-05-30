# API and Coordinates

## Functions

- [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md)
  searches once.
- [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md)
  creates a reusable searcher.
- [`sassy_searcher_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher_search.md)
  searches with that object.
- [`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md)
  reads from an R connection.
- [`sassy_features()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_features.md)
  prints backend/build information.
- [`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
  selects a backend before first use.

## Coordinates

Coordinates are 0-based and half-open.

- `text_start`, `text_end`: interval in the text.
- `pattern_start`, `pattern_end`: interval in the pattern.
- `pattern_idx`, `text_idx`: input index for vector/list inputs.

``` r

matches <- sassy_search("ACGT", "TTACGTAA", k = 0, alphabet = "dna", rc = FALSE)
matches[, c("text_start", "text_end", "pattern_start", "pattern_end", "cigar")]
#> <sassy_matches> 1 match
#> text_start text_end pattern_start pattern_end cigar
#>          2        6             0           4    4=
```

## Inputs

`pattern` and `text` may be character vectors, raw vectors, or lists of
raw vectors / character scalars. Use raw vectors for byte data.

``` r

sassy_search(charToRaw("ACGT"), charToRaw("TTACGTAA"), k = 0, alphabet = "dna")
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          2        6             0           4    0      +    4=
#>           0        0          2        6             0           4    0      -    4=
```

## Match regions

`match_region = TRUE` adds the matched text interval. Reverse-strand
regions are reverse-complemented to match the pattern direction.

``` r

sassy_search(
  "ATCGATCG",
  "GGGGATCGATCGTTTT",
  k = 1,
  alphabet = "dna",
  match_region = TRUE
)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar match_region
#>           0        0          4       12             0           8    0      +     8=     ATCGATCG
#>           0        0          6       14             0           8    1      - 1=1X6=     AACGATCG
#>           0        0          2       10             0           8    1      -   7=1X     ATCGATCC
```

## Modes

- `single`: independent pairwise searches.
- `batch_texts`: one pattern, multiple texts per batch.
- `batch_patterns`: multiple equal-length patterns per batch.
- `encoded_patterns` / `v2`: Sassy encoded-pattern path.

`batch_patterns` and `encoded_patterns` use Sassy’s multi-pattern
encoding. In `sassy` 0.2.1 that encoding is implemented for the IUPAC
profile and equal byte-length patterns. Use `single` for other alphabets
or mixed pattern lengths.
