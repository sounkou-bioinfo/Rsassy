# Print sassy match data frames

Print sassy match data frames

## Usage

``` r
# S3 method for class 'sassy_matches'
print(x, ..., color = getOption("Rsassy.coloring", FALSE))
```

## Arguments

- x:

  A `sassy_matches` data frame.

- ...:

  Ignored; accepted for compatibility with
  [`print()`](https://rdrr.io/r/base/print.html).

- color:

  If `TRUE`, color `match_region` by CIGAR operation with ANSI escape
  sequences: green matches, orange substitutions, blue inserted text,
  and red gaps for pattern bases absent from the text. Defaults to
  `getOption("Rsassy.coloring", FALSE)`.

## Value

`x`, invisibly.
