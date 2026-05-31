FASTX ALTREP Conformance Checks
================

Executable conformance checks for the chunked FASTA/FASTQ iterator. This
report is outside `vignettes/` and ignored by `R CMD build`; run it
with:

``` sh
make fastx-conformance
```

``` r
cli <- Sys.getenv("SASSY_CLI", unset = Sys.which("sassy"))
if (!nzchar(cli)) cli <- "/tmp/sassy-cli-root/bin/sassy"
cli_version <- system2(cli, "--version", stdout = TRUE)
cli_version
#> [1] "sassy 0.2.1"

cli_search <- function(args) {
  lines <- system2(cli, c("search", args), stdout = TRUE, stderr = TRUE)
  status <- attr(lines, "status")
  stopifnot(is.null(status) || identical(status, 0L))
  lines <- lines[grepl("\t", lines)]
  utils::read.delim(text = paste(lines, collapse = "\n"), stringsAsFactors = FALSE, check.names = FALSE)
}

search_to_cli <- function(x, pattern_ids, text_ids) {
  data.frame(
    pat_id = pattern_ids[x$pattern_idx + 1L],
    text_id = text_ids[x$text_idx + 1L],
    cost = as.integer(x$cost),
    strand = as.character(x$strand),
    start = as.integer(x$text_start),
    end = as.integer(x$text_end),
    match_region = as.character(x$match_region),
    cigar = as.character(x$cigar),
    stringsAsFactors = FALSE
  )
}

normalize_search <- function(x) {
  cols <- c("pat_id", "text_id", "cost", "strand", "start", "end", "match_region", "cigar")
  x <- as.data.frame(x)[cols]
  x$cost <- as.integer(x$cost)
  x$start <- as.integer(x$start)
  x$end <- as.integer(x$end)
  for (col in setdiff(cols, c("cost", "start", "end"))) x[[col]] <- as.character(x[[col]])
  x <- x[do.call(order, x), , drop = FALSE]
  row.names(x) <- NULL
  x
}

tmp <- tempfile("rsassy-fastx-conformance-")
dir.create(tmp)
```

## FASTQ batches

``` r
fastq_path <- file.path(tmp, "reads.fastq")
writeLines(c(
  "@r1", "ACGT", "+", "!!!!",
  "@r2 description", "TTTT", "+", "####",
  "@r3", "GGGGACG", "+", "IIIIIII"
), fastq_path, useBytes = TRUE)

it <- sassy_fastx_iter(fastq_path, batch_records = 2L)
batch <- sassy_fastx_next(it)
stopifnot(identical(class(batch), "sassy_fastx_batch"))
stopifnot(identical(as.character(batch$id), c("r1", "r2 description")))
stopifnot(identical(rawToChar(batch$seq[[1L]]), "ACGT"))
stopifnot(identical(rawToChar(batch$seq[[2L]]), "TTTT"))
stopifnot(identical(rawToChar(batch$qual[[2L]]), "####"))

batch2 <- sassy_fastx_next(it)
stopifnot(identical(as.character(batch2$id), "r3"))
stopifnot(is.null(sassy_fastx_next(it)))
stopifnot(is.null(sassy_fastx_next(it)))
```

## CLI baseline through FASTQ records

``` r
cli_hits <- normalize_search(cli_search(c("-p", "ACG", "-k", "0", "--alphabet", "dna", "--no-rc", fastq_path)))

r_hits <- list()
it <- sassy_fastx_iter(fastq_path, batch_records = 2L)
repeat {
  batch <- sassy_fastx_next(it)
  if (is.null(batch)) break
  r_hits[[length(r_hits) + 1L]] <- search_to_cli(
    sassy_search(list("ACG"), batch$seq, k = 0, alphabet = "dna", rc = FALSE, text_id = batch$id, match_region = TRUE),
    pattern_ids = "pattern",
    text_ids = as.character(batch$id)
  )
}
r_hits <- normalize_search(do.call(rbind, r_hits))
cli_hits
#>    pat_id text_id cost strand start end match_region cigar
#> 1 pattern      r1    0      +     0   3          ACG    3=
#> 2 pattern      r3    0      +     4   7          ACG    3=
r_hits
#>    pat_id text_id cost strand start end match_region cigar
#> 1 pattern      r1    0      +     0   3          ACG    3=
#> 2 pattern      r3    0      +     4   7          ACG    3=
stopifnot(identical(r_hits, cli_hits))
```

## ALTREP lifetime and writable-copy behavior

``` r
batch <- sassy_fastx_next(sassy_fastx_iter(fastq_path, batch_records = 2L))
kept_seq <- batch$seq[[1L]]
kept_id <- batch$id
rm(batch)
invisible(gc())
stopifnot(identical(rawToChar(kept_seq), "ACGT"))
stopifnot(identical(as.character(kept_id), c("r1", "r2 description")))

batch <- sassy_fastx_next(sassy_fastx_iter(fastq_path, batch_records = 2L))
x <- batch$seq[[1L]]
x[1L] <- charToRaw("T")
stopifnot(identical(rawToChar(x), "TCGT"))
stopifnot(identical(rawToChar(batch$seq[[1L]]), "ACGT"))
```

## Formats and encodings

``` r
noqual <- sassy_fastx_next(sassy_fastx_iter(fastq_path, batch_records = 3L, include_qual = FALSE))
stopifnot(is.null(noqual$qual))
stopifnot(identical(rawToChar(noqual$seq[[3L]]), "GGGGACG"))

fasta_path <- file.path(tmp, "wrapped-crlf.fa")
writeBin(charToRaw(">fa1\r\nAC\r\nGT\r\n>fa2\r\nTTTT\r\n"), fasta_path)
fasta_batch <- sassy_fastx_next(sassy_fastx_iter(fasta_path, batch_records = 10L))
stopifnot(identical(as.character(fasta_batch$id), c("fa1", "fa2")))
stopifnot(identical(rawToChar(fasta_batch$seq[[1L]]), "ACGT"))
stopifnot(is.null(fasta_batch$qual))

fastq_gz_path <- file.path(tmp, "reads.fastq.gz")
con <- gzfile(fastq_gz_path, open = "wb")
writeLines(readLines(fastq_path, warn = FALSE), con, useBytes = TRUE)
close(con)
gz_batch <- sassy_fastx_next(sassy_fastx_iter(fastq_gz_path, batch_records = 10L))
stopifnot(identical(as.character(gz_batch$id), c("r1", "r2 description", "r3")))
stopifnot(identical(rawToChar(gz_batch$seq[[3L]]), "GGGGACG"))

utf8_fastq_path <- file.path(tmp, enc2utf8("réads.fastq"))
writeLines(c("@réad", "ACG", "+", "!!!"), utf8_fastq_path, useBytes = TRUE)
utf8_batch <- sassy_fastx_next(sassy_fastx_iter(utf8_fastq_path, batch_records = 1L))
stopifnot(identical(as.character(utf8_batch$id), enc2utf8("réad")))
utf8_hits <- sassy_search(list("ACG"), utf8_batch$seq, 0, alphabet = "dna", rc = FALSE, text_id = utf8_batch$id)
stopifnot(identical(utf8_hits$text_id, enc2utf8("réad")))
```

## Error paths

``` r
stopifnot(inherits(try(sassy_fastx_iter(fastq_path, batch_records = 0L), silent = TRUE), "try-error"))
stopifnot(inherits(try(sassy_fastx_next(sassy_searcher("dna", rc = FALSE)), silent = TRUE), "try-error"))
```

All FASTX conformance checks passed.
