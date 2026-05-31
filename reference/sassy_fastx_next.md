# Get the next FASTA/FASTQ batch

Get the next FASTA/FASTQ batch

## Usage

``` r
sassy_fastx_next(iter)
```

## Arguments

- iter:

  An iterator created by
  [`sassy_fastx_iter()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_iter.md).

## Value

`NULL` at end of file, otherwise a `sassy_fastx_batch` list with `id`,
`seq`, and `qual` elements. `id` is an ALTREP character vector, while
`seq` and `qual` are ALTREP lists whose elements are raw ALTREP vectors.

## Examples

``` r
fq <- tempfile(fileext = ".fastq")
writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
it <- sassy_fastx_iter(fq, batch_records = 1)
batch <- sassy_fastx_next(it)
length(batch$id)
#> [1] 1
```
