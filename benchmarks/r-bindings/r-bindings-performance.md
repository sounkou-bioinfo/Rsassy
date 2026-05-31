R Binding Performance Smoke Benchmark
================

Smoke benchmark for the R binding surface. It includes upstream CLI
baselines and threaded Rsassy runs. Run with:

``` sh
make r-bindings-benchmark
```

Timings are machine-dependent; the committed Markdown is a visible
reference, not a portability threshold.

``` r
n_texts <- as.integer(Sys.getenv("RSASSY_BINDING_BENCH_TEXTS", "5000"))
text_len <- as.integer(Sys.getenv("RSASSY_BINDING_BENCH_TEXT_LEN", "200"))
threads <- as.integer(Sys.getenv("RSASSY_BINDING_BENCH_THREADS", "4"))
cli <- Sys.getenv("SASSY_CLI", unset = Sys.which("sassy"))
if (!nzchar(cli)) cli <- "/tmp/sassy-cli-root/bin/sassy"
cli_version <- system2(cli, "--version", stdout = TRUE)

data.frame(
  parameter = c("n_texts", "text_len", "threads", "sassy_cli", "sassy_cli_version"),
  value = c(n_texts, text_len, threads, cli, paste(cli_version, collapse = " "))
)
#>           parameter                         value
#> 1           n_texts                          5000
#> 2          text_len                           200
#> 3           threads                             4
#> 4         sassy_cli /tmp/sassy-cli-root/bin/sassy
#> 5 sassy_cli_version                   sassy 0.2.1
```

## Fixtures

``` r
tmp <- tempfile("rsassy-bindings-bench-")
dir.create(tmp)

make_texts <- function(n, len) {
  bases <- c("A", "C", "G", "T")
  vapply(seq_len(n), function(i) {
    x <- bases[((i + seq_len(len) - 2L) %% 4L) + 1L]
    if (i %% 7L == 0L) x[80:82] <- c("A", "C", "G")
    paste0(x, collapse = "")
  }, character(1), USE.NAMES = FALSE)
}

write_fasta <- function(records, file) {
  con <- file(file, open = "wb")
  on.exit(close(con), add = TRUE)
  for (id in names(records)) writeLines(c(paste0(">", id), records[[id]]), con, useBytes = TRUE)
  file
}

texts <- make_texts(n_texts, text_len)
text_ids <- paste0("t", seq_along(texts))
names(texts) <- text_ids
text_fa <- write_fasta(texts, file.path(tmp, "texts.fa"))
patterns <- c(p_acg = "ACG", p_ttt = "TTT", p_cgt = "CGT", p_gta = "GTA")
pattern_fa <- write_fasta(patterns, file.path(tmp, "patterns.fa"))

data.frame(file = basename(text_fa), bytes = file.info(text_fa)$size, records = length(texts))
#>       file   bytes records
#> 1 texts.fa 1038893    5000
```

## Helpers

``` r
bench_one <- function(label, expr) {
  invisible(gc())
  elapsed <- system.time(value <- force(expr))
  data.frame(
    label = label,
    seconds = unname(elapsed[["elapsed"]]),
    rows = if (is.data.frame(value)) nrow(value) else as.integer(value),
    stringsAsFactors = FALSE
  )
}

run_cli_table <- function(args) {
  lines <- system2(cli, args, stdout = TRUE, stderr = TRUE)
  status <- attr(lines, "status")
  stopifnot(is.null(status) || identical(status, 0L))
  lines <- lines[grepl("\t", lines)]
  utils::read.delim(text = paste(lines, collapse = "\n"), stringsAsFactors = FALSE, check.names = FALSE)
}
```

## Search strategies and CLI baseline

``` r
searcher <- sassy_searcher("iupac", rc = FALSE)

bench <- do.call(rbind, list(
  bench_one("cli_search_pattern_fasta", run_cli_table(c("search", "--pattern-fasta", pattern_fa, "-k", "0", "--no-rc", "--alphabet", "iupac", text_fa))),
  bench_one("cli_search_v2", run_cli_table(c("search", "--pattern-fasta", pattern_fa, "-k", "0", "--no-rc", "--alphabet", "iupac", "--v2", text_fa))),
  bench_one("r_pairwise_threads_1", sassy_searcher_search(searcher, as.list(unname(patterns)), as.list(unname(texts)), 0, strategy = "pairwise", threads = 1L)),
  bench_one("r_pairwise_threads_n", sassy_searcher_search(searcher, as.list(unname(patterns)), as.list(unname(texts)), 0, strategy = "pairwise", threads = threads)),
  bench_one("r_encoded_patterns", sassy_searcher_search(searcher, as.list(unname(patterns)), as.list(unname(texts)), 0, strategy = "encoded_patterns", threads = threads)),
  bench_one("r_batch_texts_one_pattern", sassy_search(list("ACG"), as.list(unname(texts)), 0, alphabet = "iupac", rc = FALSE, strategy = "batch_texts", threads = threads))
))
bench
#>                       label seconds   rows
#> 1  cli_search_pattern_fasta   1.879 741074
#> 2             cli_search_v2   1.685 741074
#> 3      r_pairwise_threads_1   0.263 741074
#> 4      r_pairwise_threads_n   0.197 741074
#> 5        r_encoded_patterns   0.207 741074
#> 6 r_batch_texts_one_pattern   0.075 247322
```

## Searcher reuse and raw input path

``` r
small_texts <- as.list(unname(texts[seq_len(min(1000L, length(texts)))]))
raw_texts <- lapply(small_texts, charToRaw)

reuse_bench <- do.call(rbind, list(
  bench_one("reuse_searcher_20_calls", {
    s <- sassy_searcher("dna", rc = FALSE)
    rows <- 0L
    for (i in seq_len(20L)) rows <- rows + nrow(sassy_searcher_search(s, list("ACG"), small_texts, 0, threads = threads))
    rows
  }),
  bench_one("new_searcher_20_calls", {
    rows <- 0L
    for (i in seq_len(20L)) rows <- rows + nrow(sassy_search(list("ACG"), small_texts, 0, alphabet = "dna", rc = FALSE, threads = threads))
    rows
  }),
  bench_one("raw_text_list", sassy_search(list(charToRaw("ACG")), raw_texts, 0, alphabet = "dna", rc = FALSE, threads = threads))
))
reuse_bench
#>                     label seconds   rows
#> 1 reuse_searcher_20_calls   0.202 989300
#> 2   new_searcher_20_calls   0.204 989300
#> 3           raw_text_list   0.010  49465
```

## CRISPR CLI and threaded baseline

``` r
crispr_texts <- texts[seq_len(min(1000L, length(texts)))]
crispr_fa <- write_fasta(crispr_texts, file.path(tmp, "crispr.fa"))
guide_file <- file.path(tmp, "guides.txt")
writeLines("ACGTNGG", guide_file, useBytes = TRUE)

crispr_cli <- function() {
  out <- tempfile(fileext = ".tsv")
  invisible(system2(cli, c("crispr", "--guide", guide_file, "-k", "1", "--max-n-frac", "0.2", "--no-rc", "--threads", "1", "--output", out, crispr_fa), stdout = TRUE, stderr = TRUE))
  x <- utils::read.delim(out, stringsAsFactors = FALSE, check.names = FALSE)
  unlink(out)
  x
}

crispr_bench <- do.call(rbind, list(
  bench_one("cli_crispr_threads_1", crispr_cli()),
  bench_one("r_crispr_threads_1", sassy_crispr(list("ACGTNGG"), as.list(unname(crispr_texts)), 1, max_n_frac = 0.2, rc = FALSE, threads = 1L)),
  bench_one("r_crispr_threads_n", sassy_crispr(list("ACGTNGG"), as.list(unname(crispr_texts)), 1, max_n_frac = 0.2, rc = FALSE, threads = threads))
))
crispr_bench
#>                  label seconds rows
#> 1 cli_crispr_threads_1   0.004   35
#> 2   r_crispr_threads_1   0.001   35
#> 3   r_crispr_threads_n   0.002   35
```

## Assertions

``` r
stopifnot(all(bench$rows > 0))
stopifnot(all(reuse_bench$rows > 0))
stopifnot(all(crispr_bench$rows >= 0))
```

Use environment variables to scale the benchmark:

``` sh
RSASSY_BINDING_BENCH_TEXTS=20000 RSASSY_BINDING_BENCH_THREADS=8 make r-bindings-benchmark
```
