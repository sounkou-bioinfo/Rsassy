# Create a chunked FASTA/FASTQ iterator

`sassy_fastx_iter()` opens a FASTA or FASTQ file and returns an iterator
that yields record-count-bounded batches. Parsing is performed by the
vendored Rust `needletail` parser. Sequence and quality data in each
batch are exposed as read-only raw ALTREP slices over immutable native
batch buffers; they are not eagerly materialized as R strings.

## Usage

``` r
sassy_fastx_iter(path, batch_records = 100000L, include_qual = TRUE)
```

## Arguments

- path:

  Path to a FASTA/FASTQ file. Gzip-compressed input is supported by the
  vendored `needletail` gzip backend.

- batch_records:

  Maximum number of records returned by each
  [`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md)
  call.

- include_qual:

  If `TRUE`, FASTQ qualities are included as `batch$qual`. If `FALSE`,
  or for FASTA input, `batch$qual` is `NULL`.

## Value

An external pointer with class `sassy_fastx_iter`.

## Examples

``` r
fq <- tempfile(fileext = ".fastq")
writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
it <- sassy_fastx_iter(fq, batch_records = 1)
batch <- sassy_fastx_next(it)
rawToChar(batch$seq[[1]])
#> [1] "ACGT"
```
