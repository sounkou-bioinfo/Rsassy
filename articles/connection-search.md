# Searching R Connections

[`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md)
searches an open readable R connection. Matches may cross chunk
boundaries.

## Example

``` r

tmp <- tempfile(fileext = ".gz")
con <- gzfile(tmp, "wb")
writeBin(charToRaw("CCCCATGCCCCTTT"), con)
close(con)

con <- gzfile(tmp, "rb")
sassy_search_connection(
  c("ATG", "TTT"),
  con,
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        7             0           3    0      +    3=
#>           1        0         11       14             0           3    0      +    3=
close(con)
unlink(tmp)
```

Coordinates are relative to the full stream.

## Chunking

`chunk_size` controls the number of new bytes read per chunk. If
`overlap` is `NULL`, Rsassy computes it from the longest pattern, `k`,
and `alpha`.

``` r

con <- rawConnection(charToRaw("AAAAACGTAAAA"), "rb")
sassy_search_connection(
  "ACGT",
  con,
  k = 0,
  alphabet = "dna",
  rc = FALSE,
  chunk_size = 6
)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        8             0           4    0      +    4=
close(con)
```
