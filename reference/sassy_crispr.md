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
  pattern_id = NULL,
  text_id = NULL
)
```

## Arguments

- guide:

  List of guide sequences including the PAM suffix. Each element must be
  a raw vector or non-missing character scalar.

- text:

  List of text sequences to search. Each element must be a raw vector or
  non-missing character scalar.

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

- pattern_id:

  Optional guide/pattern identifiers. If supplied, must be a character
  vector with one entry per guide and adds/replaces a `pattern_id`
  column. Names on `guide` are not inspected.

- text_id:

  Optional text identifiers. If supplied, must be a character vector
  with one entry per text and adds/replaces a `text_id` column. Names on
  `text` are not inspected.

## Value

A data frame with CLI-style columns: `guide`, `cost`, `strand`, `start`,
`end`, `match_region`, and `cigar`. If `pattern_id` or `text_id` are
supplied, mapped identifier columns are included.

## Examples

``` r
sassy_crispr(list("ACGTNGG"), list("TTTACGTAGGTTT"), k = 0, rc = FALSE, text_id = "chr1")
#>     guide text_id cost strand start end match_region cigar
#> 1 ACGTNGG    chr1    0      +     3  10      ACGTAGG    7=
```
