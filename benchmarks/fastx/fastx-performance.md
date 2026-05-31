FASTX ALTREP Performance Smoke Benchmark
================

This report is an executable smoke benchmark for the chunked FASTA/FASTQ
iterator. It is not part of `R CMD check`; run it explicitly with:

``` sh
make fastx-benchmark
```

The goal is to catch obvious allocation/materialization regressions and
to make the performance model visible in committed Markdown output.
Absolute timings are machine-dependent.

``` r
n_records <- as.integer(Sys.getenv("RSASSY_FASTX_BENCH_RECORDS", "50000"))
read_len <- as.integer(Sys.getenv("RSASSY_FASTX_BENCH_READ_LEN", "150"))
batch_records <- as.integer(Sys.getenv("RSASSY_FASTX_BENCH_BATCH", "50000"))
cli <- Sys.getenv("SASSY_CLI", unset = Sys.which("sassy"))
if (!nzchar(cli)) cli <- "/tmp/sassy-cli-root/bin/sassy"
cli_version <- system2(cli, "--version", stdout = TRUE)

config <- data.frame(
  parameter = c("n_records", "read_len", "batch_records", "sassy_cli", "sassy_cli_version"),
  value = c(n_records, read_len, batch_records, cli, paste(cli_version, collapse = " "))
)
config
#>           parameter                         value
#> 1         n_records                         50000
#> 2          read_len                           150
#> 3     batch_records                         50000
#> 4         sassy_cli /tmp/sassy-cli-root/bin/sassy
#> 5 sassy_cli_version                   sassy 0.2.1
```

## Fixture generation

``` r
tmp <- tempfile("rsassy-fastx-bench-")
dir.create(tmp)
fastq_path <- file.path(tmp, "synthetic.fastq")
fastq_gz_path <- file.path(tmp, "synthetic.fastq.gz")
fasta_path <- file.path(tmp, "wrapped.fa")

bases <- charToRaw("ACGT")
qual <- charToRaw(paste(rep("I", read_len), collapse = ""))

seq_string <- function(i, len) {
  paste0(rawToChar(bases[((i + seq_len(len) - 2L) %% 4L) + 1L], multiple = TRUE), collapse = "")
}

write_fastq <- function(path, n, len, chunk_n = 5000L) {
  con <- file(path, open = "wb")
  on.exit(close(con), add = TRUE)
  qual_string <- rawToChar(qual)
  for (start in seq.int(1L, n, by = chunk_n)) {
    idx <- start:min(n, start + chunk_n - 1L)
    seqs <- vapply(idx, seq_string, character(1), len = len, USE.NAMES = FALSE)
    writeLines(as.vector(rbind(sprintf("@r%d", idx), seqs, "+", qual_string)), con, useBytes = TRUE)
  }
}

write_wrapped_fasta <- function(path, n, len, width = 50L, chunk_n = 5000L) {
  con <- file(path, open = "wb")
  on.exit(close(con), add = TRUE)
  for (start in seq.int(1L, n, by = chunk_n)) {
    idx <- start:min(n, start + chunk_n - 1L)
    records <- unlist(lapply(idx, function(i) {
      seq_i <- seq_string(i, len)
      c(sprintf(">r%d", i), substring(seq_i, seq.int(1L, len, by = width), pmin(len, seq.int(1L, len, by = width) + width - 1L)))
    }), use.names = FALSE)
    writeLines(records, con, useBytes = TRUE)
  }
}

elapsed_generate <- system.time({
  write_fastq(fastq_path, n_records, read_len)
  write_wrapped_fasta(fasta_path, n_records, read_len)
  gz <- gzfile(fastq_gz_path, open = "wb")
  writeBin(readBin(fastq_path, what = "raw", n = file.info(fastq_path)$size), gz)
  close(gz)
})

files <- data.frame(
  file = basename(c(fastq_path, fastq_gz_path, fasta_path)),
  bytes = file.info(c(fastq_path, fastq_gz_path, fasta_path))$size
)
files
#>                 file    bytes
#> 1    synthetic.fastq 15588894
#> 2 synthetic.fastq.gz   209479
#> 3         wrapped.fa  8038894
data.frame(step = "generate", seconds = unname(elapsed_generate[["elapsed"]]))
#>       step seconds
#> 1 generate   1.593
```

## Benchmark helpers

``` r
null_file <- function() if (.Platform$OS.type == "windows") "NUL" else "/dev/null"

bench_one <- function(label, expr) {
  invisible(gc())
  gc_before <- gc()[, "used"]
  mem_before <- lobstr::mem_used()
  elapsed <- system.time(value <- force(expr))
  mem_after <- lobstr::mem_used()
  gc_after <- gc()[, "used"]
  data.frame(
    label = label,
    seconds = unname(elapsed[["elapsed"]]),
    records = value$records,
    seq_bytes = value$seq_bytes,
    qual_bytes = value$qual_bytes,
    checksum = value$checksum,
    aux_obj_bytes = if (is.null(value$aux_obj_bytes)) NA_real_ else value$aux_obj_bytes,
    result_obj_bytes = as.numeric(lobstr::obj_size(value)),
    mem_delta_bytes = as.numeric(mem_after) - as.numeric(mem_before),
    gc_ncell_delta = unname(gc_after[["Ncells"]] - gc_before[["Ncells"]]),
    gc_vcell_delta = unname(gc_after[["Vcells"]] - gc_before[["Vcells"]]),
    stringsAsFactors = FALSE
  )
}

iterate_fastx <- function(path, include_qual = TRUE, touch = c("none", "first", "all")) {
  touch <- match.arg(touch)
  it <- sassy_fastx_iter(path, batch_records = batch_records, include_qual = include_qual)
  records <- 0L
  seq_bytes <- 0
  qual_bytes <- 0
  checksum <- 0
  max_batch_obj_bytes <- 0
  repeat {
    batch <- sassy_fastx_next(it)
    if (is.null(batch)) break
    n <- length(batch$seq)
    records <- records + n
    max_batch_obj_bytes <- max(max_batch_obj_bytes, as.numeric(lobstr::obj_size(batch)))
    if (touch == "none") {
      seq_bytes <- seq_bytes + sum(vapply(seq_len(n), function(i) length(batch$seq[[i]]), integer(1)))
      if (!is.null(batch$qual)) {
        qual_bytes <- qual_bytes + sum(vapply(seq_len(n), function(i) length(batch$qual[[i]]), integer(1)))
      }
    } else if (touch == "first") {
      first_seq <- batch$seq[[1L]]
      seq_bytes <- seq_bytes + length(first_seq)
      checksum <- checksum + as.integer(first_seq[[1L]])
      if (!is.null(batch$qual)) {
        first_qual <- batch$qual[[1L]]
        qual_bytes <- qual_bytes + length(first_qual)
        checksum <- checksum + as.integer(first_qual[[1L]])
      }
    } else {
      for (i in seq_len(n)) {
        seq_i <- batch$seq[[i]]
        seq_bytes <- seq_bytes + length(seq_i)
        checksum <- checksum + as.integer(seq_i[[1L]])
        if (!is.null(batch$qual)) {
          qual_i <- batch$qual[[i]]
          qual_bytes <- qual_bytes + length(qual_i)
          checksum <- checksum + as.integer(qual_i[[1L]])
        }
      }
    }
  }
  list(
    records = records,
    seq_bytes = seq_bytes,
    qual_bytes = qual_bytes,
    checksum = checksum,
    aux_obj_bytes = max_batch_obj_bytes
  )
}

cli_search_file <- function(path, expected_hits) {
  status <- system2(
    cli,
    c("search", "-p", "ACG", "-k", "0", "--alphabet", "dna", "--no-rc", path),
    stdout = null_file(),
    stderr = null_file()
  )
  stopifnot(identical(status, 0L))
  list(
    records = n_records,
    seq_bytes = n_records * read_len,
    qual_bytes = n_records * read_len,
    checksum = expected_hits,
    aux_obj_bytes = 0
  )
}

readlines_fastq <- function(path) {
  lines <- readLines(path, warn = FALSE)
  seq <- lines[seq.int(2L, length(lines), by = 4L)]
  qual <- lines[seq.int(4L, length(lines), by = 4L)]
  list(
    records = length(seq),
    seq_bytes = sum(nchar(seq, type = "bytes")),
    qual_bytes = sum(nchar(qual, type = "bytes")),
    checksum = sum(as.integer(charToRaw(paste0(substr(seq, 1L, 1L), collapse = "")))) +
      sum(as.integer(charToRaw(paste0(substr(qual, 1L, 1L), collapse = "")))),
    aux_obj_bytes = as.numeric(lobstr::obj_size(lines, seq, qual))
  )
}
```

## Iteration timings

``` r
bench <- do.call(rbind, list(
  bench_one("fastq_altrep_no_qual_lengths", iterate_fastx(fastq_path, include_qual = FALSE, touch = "none")),
  bench_one("fastq_altrep_with_qual_lengths", iterate_fastx(fastq_path, include_qual = TRUE, touch = "none")),
  bench_one("fastq_altrep_touch_all", iterate_fastx(fastq_path, include_qual = TRUE, touch = "all")),
  bench_one("fastq_gz_altrep_with_qual_lengths", iterate_fastx(fastq_gz_path, include_qual = TRUE, touch = "none")),
  bench_one("wrapped_fasta_altrep_lengths", iterate_fastx(fasta_path, include_qual = FALSE, touch = "none")),
  bench_one("base_readLines_fastq", readlines_fastq(fastq_path))
))
bench
#>                               label seconds records seq_bytes qual_bytes
#> 1      fastq_altrep_no_qual_lengths   0.072   50000   7500000          0
#> 2    fastq_altrep_with_qual_lengths   0.112   50000   7500000    7500000
#> 3            fastq_altrep_touch_all   0.133   50000   7500000    7500000
#> 4 fastq_gz_altrep_with_qual_lengths   0.111   50000   7500000    7500000
#> 5      wrapped_fasta_altrep_lengths   0.063   50000   7500000          0
#> 6              base_readLines_fastq   0.102   50000   7500000    7500000
#>   checksum aux_obj_bytes result_obj_bytes mem_delta_bytes gc_ncell_delta
#> 1        0          1976              896           65608            834
#> 2        0          2104              896            1792             24
#> 3  7237500          2104              896            1792             24
#> 4        0          2104              896            1792             24
#> 5        0          1976              896            1792             24
#> 6  7237500       5201200              896           88768           1148
#>   gc_vcell_delta
#> 1           2877
#> 2             43
#> 3             43
#> 4             43
#> 5             43
#> 6           3088
```

## Search-through-batches smoke check

``` r
search_batches <- function(path) {
  it <- sassy_fastx_iter(path, batch_records = batch_records, include_qual = FALSE)
  records <- 0L
  seq_bytes <- 0
  hits <- 0L
  max_batch_obj_bytes <- 0
  repeat {
    batch <- sassy_fastx_next(it)
    if (is.null(batch)) break
    n <- length(batch$seq)
    records <- records + n
    seq_bytes <- seq_bytes + sum(vapply(seq_len(n), function(i) length(batch$seq[[i]]), integer(1)))
    max_batch_obj_bytes <- max(max_batch_obj_bytes, as.numeric(lobstr::obj_size(batch)))
    out <- sassy_search(list("ACG"), batch$seq, k = 0, alphabet = "dna", rc = FALSE, text_id = batch$id)
    hits <- hits + nrow(out)
  }
  list(
    records = records,
    seq_bytes = seq_bytes,
    qual_bytes = 0,
    checksum = hits,
    aux_obj_bytes = max_batch_obj_bytes
  )
}

expected_hits <- search_batches(fastq_path)$checksum
search_bench <- do.call(rbind, list(
  bench_one("search_acg_over_fastq_batches", search_batches(fastq_path)),
  bench_one("cli_search_acg_fastq_discard_stdout", cli_search_file(fastq_path, expected_hits)),
  bench_one("cli_search_acg_fastq_gz_discard_stdout", cli_search_file(fastq_gz_path, expected_hits))
))
search_bench
#>                                    label seconds records seq_bytes qual_bytes
#> 1          search_acg_over_fastq_batches   0.844   50000   7500000          0
#> 2    cli_search_acg_fastq_discard_stdout   0.734   50000   7500000    7500000
#> 3 cli_search_acg_fastq_gz_discard_stdout   0.731   50000   7500000    7500000
#>   checksum aux_obj_bytes result_obj_bytes mem_delta_bytes gc_ncell_delta
#> 1  1850000          1976              896            2288             23
#> 2  1850000             0              896            2200             34
#> 3  1850000             0              896            5376             61
#>   gc_vcell_delta
#> 1             42
#> 2             91
#> 3            299
```

## Assertions

``` r
stopifnot(all(bench$records == n_records))
stopifnot(all(search_bench$records == n_records))
stopifnot(all(search_bench$checksum > 0))
stopifnot(!any(is.na(search_bench$seq_bytes)))
stopifnot(bench$seq_bytes[bench$label == "fastq_altrep_no_qual_lengths"] == n_records * read_len)
stopifnot(bench$qual_bytes[bench$label == "fastq_altrep_no_qual_lengths"] == 0)
stopifnot(bench$qual_bytes[bench$label == "fastq_altrep_with_qual_lengths"] == n_records * read_len)
```

## Interpretation

The most important rows are the ALTREP iterator rows. They should show
bounded batch iteration without eagerly creating one R string per
sequence. The `base_readLines_fastq` row is not a formal competitor for
all FASTX semantics; it is a familiar eager-materialization baseline for
this synthetic uncompressed FASTQ file.

Use environment variables to scale the smoke benchmark:

``` sh
RSASSY_FASTX_BENCH_RECORDS=1000000 RSASSY_FASTX_BENCH_BATCH=100000 make fastx-benchmark
```
