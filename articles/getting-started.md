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

sassy_search(list("ATCGATCG"), list("GGGGATCGATCGTTTT"), k = 1, alphabet = "dna")
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          2       10             0           8    1      -   7=1X
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
```

The result is a `sassy_matches` data frame. Coordinates are 0-based and
half-open.

## Reuse a searcher

``` r

searcher <- sassy_searcher("dna", rc = TRUE)
sassy_searcher_search(searcher, list("ATCGATCG"), list("GGGGATCGATCGTTTT"), k = 1)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          2       10             0           8    1      -   7=1X
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
```

## Multiple patterns or texts

List inputs search every pattern against every text. Each list element
may be a raw vector or a non-missing character scalar, so callers can
mix byte strings, regular strings, and ALTREP-backed raw elements.
`pattern_idx` and `text_idx` identify the input indices.

``` r

sassy_search(
  list("ATG", charToRaw("TTT")),
  list("CCCCATGCCCCTTT"),
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  strategy = "encoded_patterns"
)
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        7             0           3    0      +    3=
#>           1        0         11       14             0           3    0      +    3=
```

## FASTA/FASTQ batches

[`sassy_fastx_iter()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_iter.md)
and
[`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md)
parse FASTA/FASTQ files into record-count-bounded batches. Record IDs
are exposed as an ALTREP character vector and sequences as a list of raw
ALTREP slices over immutable native batch buffers.

``` r

fq <- tempfile(fileext = ".fastq")
writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
it <- sassy_fastx_iter(fq, batch_records = 1)
batch <- sassy_fastx_next(it)
sassy_search(list("ACG"), batch$seq, k = 0, alphabet = "dna", rc = FALSE, text_id = batch$id)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_id text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0      r1          0        3             0           3    0      +    3=
```

See
[`vignette("fastx-iteration", package = "Rsassy")`](https://sounkou-bioinfo.github.io/Rsassy/articles/fastx-iteration.md)
for the performance and validation details.
