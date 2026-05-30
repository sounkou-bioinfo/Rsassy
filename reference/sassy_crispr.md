# Search CRISPR guide targets

`sassy_crispr()` is an R-level equivalent of the upstream `sassy crispr`
workflow for in-memory sequences. Guides include the PAM at the end. By
default, the PAM must match exactly under IUPAC matching, while the rest
of the guide may have up to `k` edits.

## Usage

``` r
sassy_crispr(
  guide,
  text,
  k,
  pam_length = 3L,
  allow_pam_edits = FALSE,
  max_n_frac = 0.2,
  rc = TRUE,
  threads = 1L,
  text_id = NULL
)
```

## Arguments

- guide:

  Character vector of guide sequences including the PAM suffix.

- text:

  Text sequences to search; a character vector, raw vector, or list of
  raw/character scalars accepted by
  [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md).

- k:

  Maximum edit distance for the searched guide sequence. With
  `allow_pam_edits = FALSE`, the exact-PAM filter means this is
  effectively the edit threshold outside the PAM.

- pam_length:

  Length of the PAM suffix.

- allow_pam_edits:

  If `TRUE`, do not require an exact PAM match.

- max_n_frac:

  Maximum allowed fraction of `N` bases in `match_region`.

- rc:

  If `TRUE`, search reverse-complement targets as well.

- threads:

  Number of worker threads to request.

- text_id:

  Optional text identifiers. Defaults to names on `text` when all names
  are non-empty, otherwise `text_1`, `text_2`, ...

## Value

A data frame with CLI-style columns: `guide`, `text_id`, `cost`,
`strand`, `start`, `end`, `match_region`, and `cigar`.

## Examples

``` r
sassy_crispr("ACGTNGG", c(chr1 = "TTTACGTAGGTTT"), k = 0, rc = FALSE)
#>     guide text_id cost strand start end match_region cigar
#> 1 ACGTNGG    chr1    0      +     3  10      ACGTAGG    7=
```
