# API and Coordinates

## Functions

- [`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md)
  searches once.
- [`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md)
  creates a reusable searcher.
- [`sassy_searcher_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher_search.md)
  searches with that object.
- [`sassy_features()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_features.md)
  prints backend/build information.
- [`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
  selects a backend before first use.
- [`sassy_fastx_iter()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_iter.md)
  opens a chunked FASTA/FASTQ iterator.
- [`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md)
  returns the next record-count-bounded batch.

## Coordinates

Coordinates are 0-based and half-open.

- `text_start`, `text_end`: interval in the text.
- `pattern_start`, `pattern_end`: interval in the pattern.
- `pattern_idx`, `text_idx`: input index for vector/list inputs.

``` r

matches <- sassy_search(list("ACGT"), list("TTACGTAA"), k = 0, alphabet = "dna", rc = FALSE)
matches[, c("text_start", "text_end", "pattern_start", "pattern_end", "cigar")]
#> <sassy_matches> 1 match
#> text_start text_end pattern_start pattern_end cigar
#>          2        6             0           4    4=
```

## Inputs

`pattern` and `text` are lists of sequence elements. Each element may be
a raw vector or a non-missing character scalar. This keeps one raw
vector as one sequence of bytes and lets callers mix raw bytes, ordinary
strings, and ALTREP-backed raw elements in the same input list.

``` r

sassy_search(list(charToRaw("ACGT")), list(charToRaw("TTACGTAA")), k = 0, alphabet = "dna")
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          2        6             0           4    0      +    4=
#>           0        0          2        6             0           4    0      -    4=
```

The FASTA/FASTQ iterator returns batches already shaped for this API:
`batch$seq` is a list of raw ALTREP sequence elements and `batch$id` is
an ALTREP character vector suitable for `text_id`.

``` r

fq <- tempfile(fileext = ".fastq")
writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
batch <- sassy_fastx_next(sassy_fastx_iter(fq, batch_records = 1))
sassy_search(list("ACG"), batch$seq, k = 0, alphabet = "dna", rc = FALSE, text_id = batch$id)
#> <sassy_matches> 1 match
#> pattern_idx text_idx text_id text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0      r1          0        3             0           3    0      +    3=
```

## Match regions

`match_region = TRUE` adds the matched text interval. Reverse-strand
regions are reverse-complemented to match the pattern direction.

``` r

sassy_search(
  list("ATCGATCG"),
  list("GGGGATCGATCGTTTT"),
  k = 1,
  alphabet = "dna",
  match_region = TRUE
)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar match_region
#>           0        0          2       10             0           8    1      -   7=1X     ATCGATCC
#>           0        0          4       12             0           8    0      +     8=     ATCGATCG
#>           0        0          6       14             0           8    1      - 1=1X6=     AACGATCG
```

## Search strategies

The default `strategy = "pairwise"` searches each pattern/text pair
independently. This is the general path and works with mixed pattern
lengths and all alphabets. Other strategies are performance-oriented
paths that call different Sassy kernels:

- `batch_texts`: one pattern, multiple texts per batch.
- `batch_patterns`: multiple equal-length patterns per batch.
- `encoded_patterns` / `v2`: Sassy encoded-pattern path.

`batch_patterns` and `encoded_patterns` use Sassy’s multi-pattern
encoding. In `sassy` 0.2.1 that encoding is implemented for the IUPAC
profile and equal byte-length patterns.
