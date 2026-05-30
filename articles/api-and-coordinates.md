# API and Coordinates

`Rsassy` keeps the R surface small and close to the Rust API.

## Main functions

- [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md)
  is the convenience wrapper for one-off calls.
- [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md)
  creates a reusable searcher.
- [`sassy_searcher_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher_search.md)
  searches with a reusable searcher.
- [`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md)
  streams from an R connection.
- [`sassy_features()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_features.md)
  reports selected backend and CPU/build features.
- [`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
  requests a backend before the first native call.

## Coordinates

Match coordinates are 0-based and half-open:

- `text_start`, `text_end` describe the interval in the input text.
- `pattern_start`, `pattern_end` describe the interval in the input
  pattern.
- `pattern_idx`, `text_idx` are 0-based indices into vector/list inputs.

This mirrors the Rust crate and avoids repeated conversion at the native
boundary.

``` r

matches <- sassy_search("ACGT", "TTACGTAA", k = 0, alphabet = "dna", rc = FALSE)
matches[, c("text_start", "text_end", "pattern_start", "pattern_end", "cigar")]
```

## Input types

`pattern` and `text` can be character vectors, raw vectors, or lists of
raw vectors / character scalars. Use raw vectors when input is already
byte-oriented and should not pass through R string encoding.

``` r

sassy_search(charToRaw("ACGT"), charToRaw("TTACGTAA"), k = 0, alphabet = "dna")
```

## Match regions

Set `match_region = TRUE` to include the matched text interval.
Reverse-strand regions are reverse-complemented so the region and CIGAR
are in the input pattern direction.

``` r

sassy_search(
  "ATCGATCG",
  "GGGGATCGATCGTTTT",
  k = 1,
  alphabet = "dna",
  match_region = TRUE
)
```

## Search modes

`mode = "single"` searches each pattern/text pair independently.
`mode = "batch_texts"`, `mode = "batch_patterns"`, and
`mode = "encoded_patterns"` expose Sassy’s batched paths where
applicable. The batched pattern modes currently require equal pattern
byte lengths and the IUPAC profile.
