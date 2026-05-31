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
null_file <- function() if (.Platform$OS.type == "windows") "NUL" else "/dev/null"

bench_one <- function(label, expr) {
  invisible(gc())
  mem_before <- lobstr::mem_used()
  elapsed <- system.time(value <- force(expr))
  mem_after <- lobstr::mem_used()
  rows <- if (is.data.frame(value)) nrow(value) else if (is.list(value) && !is.null(value$rows)) value$rows else as.integer(value)
  data.frame(
    label = label,
    seconds = unname(elapsed[["elapsed"]]),
    rows = rows,
    result_obj_bytes = as.numeric(lobstr::obj_size(value)),
    mem_delta_bytes = as.numeric(mem_after) - as.numeric(mem_before),
    stringsAsFactors = FALSE
  )
}

run_cli_discard <- function(args, rows) {
  status <- system2(cli, args, stdout = null_file(), stderr = null_file())
  stopifnot(identical(status, 0L))
  list(rows = rows)
}
```

## Search strategies and CLI baseline

``` r
searcher <- sassy_searcher("iupac", rc = FALSE)
patterns_list <- as.list(unname(patterns))
texts_list <- as.list(unname(texts))
expected_pairwise_rows <- nrow(sassy_searcher_search(searcher, patterns_list, texts_list, 0, strategy = "pairwise", threads = 1L))
expected_v2_rows <- nrow(sassy_searcher_search(searcher, patterns_list, texts_list, 0, strategy = "encoded_patterns", threads = threads))
expected_one_pattern_rows <- nrow(sassy_search(list("ACG"), texts_list, 0, alphabet = "iupac", rc = FALSE, strategy = "batch_texts", threads = threads))

bench <- do.call(rbind, list(
  bench_one("cli_many_patterns_discard_stdout", run_cli_discard(c("search", "--pattern-fasta", pattern_fa, "-k", "0", "--no-rc", "--alphabet", "iupac", text_fa), expected_pairwise_rows)),
  bench_one("cli_many_patterns_v2_discard_stdout", run_cli_discard(c("search", "--pattern-fasta", pattern_fa, "-k", "0", "--no-rc", "--alphabet", "iupac", "--v2", text_fa), expected_v2_rows)),
  bench_one("r_pairwise_threads_1", sassy_searcher_search(searcher, patterns_list, texts_list, 0, strategy = "pairwise", threads = 1L)),
  bench_one("r_pairwise_threads_n", sassy_searcher_search(searcher, patterns_list, texts_list, 0, strategy = "pairwise", threads = threads)),
  bench_one("r_encoded_patterns", sassy_searcher_search(searcher, patterns_list, texts_list, 0, strategy = "encoded_patterns", threads = threads)),
  bench_one("cli_one_pattern_discard_stdout", run_cli_discard(c("search", "-p", "ACG", "-k", "0", "--no-rc", "--alphabet", "iupac", text_fa), expected_one_pattern_rows)),
  bench_one("r_batch_texts_one_pattern_acg", sassy_search(list("ACG"), texts_list, 0, alphabet = "iupac", rc = FALSE, strategy = "batch_texts", threads = threads))
))
bench
#>                                 label seconds   rows result_obj_bytes
#> 1    cli_many_patterns_discard_stdout   0.442 741074              336
#> 2 cli_many_patterns_v2_discard_stdout   0.405 741074              336
#> 3                r_pairwise_threads_1   0.252 741074         44466472
#> 4                r_pairwise_threads_n   0.203 741074         44466472
#> 5                  r_encoded_patterns   0.203 741074         44466472
#> 6      cli_one_pattern_discard_stdout   0.152 247322              336
#> 7       r_batch_texts_one_pattern_acg   0.076 247322         14841352
#>   mem_delta_bytes
#> 1           65760
#> 2           79784
#> 3        44467232
#> 4        44467232
#> 5        44467232
#> 6            1168
#> 7        14842672
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
#>                     label seconds   rows result_obj_bytes mem_delta_bytes
#> 1 reuse_searcher_20_calls   0.201 989300               56           23136
#> 2   new_searcher_20_calls   0.199 989300               56            1552
#> 3           raw_text_list   0.009  49465          2969944         2971320
```

## CRISPR CLI and threaded baseline

``` r
crispr_texts <- texts[seq_len(min(1000L, length(texts)))]
crispr_fa <- write_fasta(crispr_texts, file.path(tmp, "crispr.fa"))
guide_file <- file.path(tmp, "guides.txt")
writeLines("ACGTNGG", guide_file, useBytes = TRUE)

crispr_texts_list <- as.list(unname(crispr_texts))
expected_crispr_rows <- nrow(sassy_crispr(list("ACGTNGG"), crispr_texts_list, 1, max_n_frac = 0.2, rc = FALSE, threads = 1L))
crispr_cli <- function() {
  status <- system2(
    cli,
    c("crispr", "--guide", guide_file, "-k", "1", "--max-n-frac", "0.2", "--no-rc", "--threads", "1", "--output", null_file(), crispr_fa),
    stdout = null_file(),
    stderr = null_file()
  )
  stopifnot(identical(status, 0L))
  list(rows = expected_crispr_rows)
}

crispr_bench <- do.call(rbind, list(
  bench_one("cli_crispr_discard_output", crispr_cli()),
  bench_one("r_crispr_threads_1", sassy_crispr(list("ACGTNGG"), crispr_texts_list, 1, max_n_frac = 0.2, rc = FALSE, threads = 1L)),
  bench_one("r_crispr_threads_n", sassy_crispr(list("ACGTNGG"), crispr_texts_list, 1, max_n_frac = 0.2, rc = FALSE, threads = threads))
))
crispr_bench
#>                       label seconds rows result_obj_bytes mem_delta_bytes
#> 1 cli_crispr_discard_output   0.006   35              336            1280
#> 2        r_crispr_threads_1   0.001   35             3520            4000
#> 3        r_crispr_threads_n   0.001   35             3520            4000
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
