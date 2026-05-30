# Format matches in SAM-compatible text direction

Rsassy normally follows the upstream `sassy` TSV convention:
reverse-strand `match_region` values are reverse-complemented and CIGAR
strings are oriented in the input pattern direction. `sassy_as_sam()`
converts reverse-strand rows to the text direction used by SAM and by
upstream `sassy --sam` output.

## Usage

``` r
sassy_as_sam(x, alphabet = "dna")
```

## Arguments

- x:

  A `sassy_matches` data frame.

- alphabet:

  Alphabet profile used for the search. One of `"dna"` or `"iupac"` when
  `x` includes `match_region`.

## Value

A copy of `x` with reverse-strand `cigar` values reversed and, when
present, reverse-strand `match_region` values reverse-complemented back
to text direction.

## Examples

``` r
sassy_as_sam(
  sassy_search("ACGA", "TTTCGTTT", 0, alphabet = "dna", match_region = TRUE),
  alphabet = "dna"
)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar match_region
#>           0        0          2        6             0           4    0      -    4=         TCGT
```
