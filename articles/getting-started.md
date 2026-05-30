# Getting Started with Rsassy

`Rsassy` exposes the Rust `sassy` approximate string matcher through R’s
native C API. It is useful for short-pattern approximate search over
DNA, IUPAC, and ASCII alphabets.

## Installation

Install from r-universe:

``` r

install.packages(
  "Rsassy",
  repos = c("https://sounkou-bioinfo.r-universe.dev", "https://cloud.r-project.org")
)
```

A checkout install resolves the `sassy` Rust crate from crates.io:

``` sh
R CMD INSTALL .
```

## A minimal search

``` r

library(Rsassy)

sassy_search(
  pattern = "ATCGATCG",
  text = "GGGGATCGATCGTTTT",
  k = 1,
  alphabet = "dna"
)
```

The result is a `sassy_matches` data frame. Coordinates are 0-based and
half-open, following the Rust library.

## Reusing a searcher

Create a searcher once when multiple calls share the same alphabet and
reverse-complement behavior:

``` r

searcher <- sassy_searcher("dna", rc = TRUE)
sassy_searcher_search(searcher, "ATCGATCG", "GGGGATCGATCGTTTT", k = 1)
```

## Searching multiple inputs

Character vectors search every pattern against every text. The
`pattern_idx` and `text_idx` columns identify the 0-based input indices.

``` r

sassy_search(
  c("ATG", "TTT"),
  "CCCCATGCCCCTTT",
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
```
