# Search an R connection with 'sassy'

Streams bytes from an already-open readable R connection through the C/R
API boundary. This avoids an R-level
[`readBin()`](https://rdrr.io/r/base/readBin.html)/[`readChar()`](https://rdrr.io/r/base/readChar.html)
loop while still preserving matches that cross chunk boundaries by
keeping an overlap window. `pattern` may be a single sequence or a
vector/list of sequences, with `pattern_idx` identifying the 0-based
input pattern index.

## Usage

``` r
sassy_search_connection(
  pattern,
  con,
  k,
  alphabet = "dna",
  rc = TRUE,
  alpha = NULL,
  all = FALSE,
  threads = 1L,
  mode = "single",
  chunk_size = 1024 * 1024,
  overlap = NULL,
  match_region = FALSE,
  sam = FALSE
)
```

## Arguments

- pattern:

  Raw vector, character vector, or list of raw vectors / character
  scalars.

- con:

  An open readable R connection, preferably opened in binary mode.

- k:

  Maximum edit distance.

- alphabet:

  Alphabet profile. One of `"dna"`, `"iupac"`, or `"ascii"`.

- rc:

  If `TRUE`, search reverse-complement strand as well where supported.

- alpha:

  Optional IUPAC overhang cost in `[0, 1]`. Use `NULL` to disable.

- all:

  If `FALSE`, return local-minimum matches. If `TRUE`, return all end
  positions with score \<= `k`.

- threads:

  Number of worker threads to request for bulk searches.

- mode:

  Bulk search mode. `"single"` searches each pair independently;
  `"batch_texts"` uses one text per SIMD lane. `"batch_patterns"` and
  `"encoded_patterns"` (alias `"v2"`) use Sassy's multi-pattern
  encoding, which in `sassy` 0.2.1 is implemented for
  `alphabet = "iupac"` and equal byte-length patterns.

- chunk_size:

  Number of new bytes to read per native chunk.

- overlap:

  Number of bytes to carry from one chunk to the next. If `NULL`, C
  computes a default from the longest pattern, `k`, and `alpha`.

- match_region:

  If `TRUE`, include a `match_region` column. Reverse-strand regions are
  reverse-complemented so the region and CIGAR are in the input pattern
  direction.

- sam:

  If `TRUE`, format reverse-strand `match_region` and `cigar` in the
  text direction used by SAM and by the upstream `sassy --sam` output.

## Value

A data frame of matches with coordinates relative to the full stream.
