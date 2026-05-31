# Chunked FASTA/FASTQ Iteration

Rsassy’s search API remains sequence-list based:
[`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md)
consumes lists of raw vectors or character scalars. FASTA/FASTQ input is
provided by a separate chunked iterator that turns file records into
those sequence-list batches.

``` r

fq <- tempfile(fileext = ".fastq")
writeLines(c(
  "@read1",
  "ACGTACGT",
  "+",
  "!!!!!!!!",
  "@read2",
  "TTTTACG",
  "+",
  "#######"
), fq, useBytes = TRUE)

it <- sassy_fastx_iter(fq, batch_records = 2)
batch <- sassy_fastx_next(it)
```

A batch is a small list:

``` r

names(batch)
#> [1] "id"   "seq"  "qual"
as.character(batch$id)
#> [1] "read1" "read2"
rawToChar(batch$seq[[1]])
#> [1] "ACGTACGT"
rawToChar(batch$qual[[1]])
#> [1] "!!!!!!!!"
```

`batch$id` is an ALTREP character vector. `batch$seq` and `batch$qual`
are ALTREP lists whose elements are raw ALTREP vectors. The raw elements
are backed by immutable native batch buffers.

## Searching a batch

Pass `batch$seq` directly as the `text` list and use `batch$id` as
explicit metadata.

``` r

sassy_search(
  list("ACG"),
  batch$seq,
  k = 0,
  alphabet = "dna",
  rc = FALSE,
  text_id = batch$id
)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_id text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0   read1          0        3             0           3    0      +    3=
#>           0        0   read1          4        7             0           3    0      +    3=
#>           0        1   read2          4        7             0           3    0      +    3=
```

Call
[`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md)
repeatedly until it returns `NULL`.

``` r

sassy_fastx_next(it)
#> NULL
```

## Qualities

Quality strings are optional because Rsassy search does not use them.
Set `include_qual = FALSE` to avoid retaining quality bytes in each
batch.

``` r

it_no_qual <- sassy_fastx_iter(fq, batch_records = 2, include_qual = FALSE)
batch_no_qual <- sassy_fastx_next(it_no_qual)
is.null(batch_no_qual$qual)
#> [1] TRUE
```

For FASTA input, `batch$qual` is always `NULL`.

``` r

fa <- tempfile(fileext = ".fa")
writeLines(c(">seq1", "AC", "GT", ">seq2", "TTTT"), fa, useBytes = TRUE)
fa_batch <- sassy_fastx_next(sassy_fastx_iter(fa, batch_records = 10))
as.character(fa_batch$id)
#> [1] "seq1" "seq2"
rawToChar(fa_batch$seq[[1]])
#> [1] "ACGT"
is.null(fa_batch$qual)
#> [1] TRUE
```

Wrapped FASTA sequence lines are copied into the batch sequence slab
while stripping `\r` and `\n`. This avoids calling needletail’s
per-record [`seq()`](https://rdrr.io/r/base/seq.html) allocation path
for wrapped FASTA.

## Gzip input

Gzip-compressed FASTA/FASTQ files are supported by the vendored
needletail gzip backend.

``` r

fq_gz <- tempfile(fileext = ".fastq.gz")
con <- gzfile(fq_gz, open = "wb")
writeLines(readLines(fq, warn = FALSE), con, useBytes = TRUE)
close(con)

gz_batch <- sassy_fastx_next(sassy_fastx_iter(fq_gz, batch_records = 10))
as.character(gz_batch$id)
#> [1] "read1" "read2"
```

Plain gzip input is still sequential. Parallel random access to ordinary
`fastq.gz` would require a separate auxiliary index or cache and is not
part of this iterator.

## Performance model

The iterator is record-count bounded by `batch_records`. Each call to
[`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md)
parses up to that many records and stores IDs, sequences, and optional
qualities in native slab buffers plus offset/length arrays.

For uncompressed input and gzip input alike, Rsassy avoids materializing
sequence strings in R. The hot path is:

``` text
needletail record -> native batch slabs -> raw ALTREP slices -> Rsassy byte slices
```

For gzip, decompression itself is unavoidable, but there is no
additional R character-vector materialization of sequences. On read-only
raw access, Rsassy obtains a pointer to the native slab before Rust
worker threads start. Writable raw-vector access gets a private R-owned
copy, so user mutation cannot change the shared batch buffer.

In local synthetic checks used during development, needletail record
iteration allocated only a handful of times for FASTQ counting. Slab
batches avoided per-record `Vec` allocations and scaled peak memory with
`batch_records`; for example, batches of 10k, 50k, 100k, and 250k short
reads used progressively larger bounded slabs with similar parse time.
These numbers are hardware- and file-dependent, so package tests assert
semantics rather than benchmark times.

## Validation story

Tinytest coverage exercises:

- FASTQ batching by record count;
- gzip FASTQ input;
- wrapped FASTA input with CR/LF stripping through the slab path;
- `include_qual = FALSE`;
- direct search over `batch$seq` with `text_id = batch$id`;
- UTF-8 record ID round-tripping;
- extracted ALTREP IDs/raw slices surviving after the parent batch
  object is removed and garbage collection runs;
- writable raw-vector access copying instead of mutating the shared
  slab;
- rejecting non-iterator external pointers passed to
  [`sassy_fastx_next()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_fastx_next.md).

The package check also rebuilds the offline vendored Rust bundle and
runs the same tinytest suite under `R CMD check`.
