# Getting Started with Rsassy

`Rsassy` provides R bindings to the Rust `sassy` approximate string
matcher. It searches short patterns in DNA, IUPAC, or ASCII text.

## Install

``` r

install.packages(
  "Rsassy",
  repos = c("https://sounkou-bioinfo.r-universe.dev", "https://cloud.r-project.org")
)
```

## Search

``` r

library(Rsassy)

sassy_search("ATCGATCG", "GGGGATCGATCGTTTT", k = 1, alphabet = "dna")
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
#>           0        0          2       10             0           8    1      -   7=1X
```

The result is a `sassy_matches` data frame. Coordinates are 0-based and
half-open.

## Reuse a searcher

``` r

searcher <- sassy_searcher("dna", rc = TRUE)
sassy_searcher_search(searcher, "ATCGATCG", "GGGGATCGATCGTTTT", k = 1)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
#>           0        0          2       10             0           8    1      -   7=1X
```

## Multiple patterns or texts

Vector inputs search every pattern against every text. `pattern_idx` and
`text_idx` identify the input indices.

``` r

sassy_search(
  c("ATG", "TTT"),
  "CCCCATGCCCCTTT",
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        7             0           3    0      +    3=
#>           1        0         11       14             0           3    0      +    3=
```
