# Create a reusable 'sassy' searcher

A searcher stores the selected alphabet profile and reverse-complement
behavior. Reuse a searcher when searching many patterns or texts with
the same settings.

## Usage

``` r
sassy_searcher(alphabet = "dna", rc = TRUE, alpha = NULL)
```

## Arguments

- alphabet:

  Alphabet profile. One of `"dna"`, `"iupac"`, or `"ascii"`.

- rc:

  If `TRUE`, search reverse-complement strand as well where supported.

- alpha:

  Optional IUPAC overhang cost in `[0, 1]`. Use `NULL` to disable.

## Value

An external pointer with class `sassy_searcher`.

## Examples

``` r
searcher <- sassy_searcher("dna", rc = FALSE)
sassy_searcher_search(searcher, list("ACGT"), list("TTACGTAA"), 0)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          2        6             0           4    0      +    4=
```
