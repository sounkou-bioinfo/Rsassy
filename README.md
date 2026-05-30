
<!-- README.md is generated from README.Rmd. Please edit that file. -->

# Rsassy

[![R-universe](https://sounkou-bioinfo.r-universe.dev/badges/Rsassy)](https://sounkou-bioinfo.r-universe.dev/Rsassy)

R bindings to Sassy through R’s native C API. Results are returned as
data frames with 0-based coordinates and CIGAR strings.

## Install

Install from r-universe:

``` r
install.packages(
  "Rsassy",
  repos = c("https://sounkou-bioinfo.r-universe.dev", "https://cloud.r-project.org")
)
```

Source installs require Cargo/rustc \>= 1.91 and `xz`.

From a checkout:

``` sh
R CMD INSTALL .
```

Source packages include vendored Rust crates and build offline. Native
Rsassy builds are multiversion by default: x86_64 builds install scalar,
AVX2, and AVX-512 backends; arm64 builds install scalar and NEON
backends. Rsassy selects the best installed backend supported by the
current CPU/runtime when the native backend is first loaded.
webR/WebAssembly builds use `wasm32-unknown-emscripten` with SIMD128.

## Usage

``` r
library(Rsassy)

sassy_search("ATCGATCG", "GGGGATCGATCGTTTT", k = 1, alphabet = "dna")
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
#>           0        0          2       10             0           8    1      -   7=1X
```

The result is a `sassy_matches` data frame with `pattern_idx`,
`text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`,
`cost`, `strand`, and `cigar`. Coordinates are 0-based and half-open.

Set `match_region = TRUE` when you also want the matched sequence. For
`strand == "-"`, `match_region` is reverse-complemented so it is in the
same direction as the input pattern and CIGAR.

``` r
region_matches <- sassy_search(
  "ATCGATCG",
  "GGGGATCGATCGTTTT",
  k = 1,
  alphabet = "dna",
  match_region = TRUE
)
region_matches
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar match_region
#>           0        0          4       12             0           8    0      +     8=     ATCGATCG
#>           0        0          6       14             0           8    1      - 1=1X6=     AACGATCG
#>           0        0          2       10             0           8    1      -   7=1X     ATCGATCC
```

The print method can color `match_region` with simple ANSI escape
sequences, following the CLI `sassy grep` alignment legend: green for
matching characters, orange for mismatches, blue for inserted text
characters, and red gaps for pattern characters absent from the text.
Coloring is off by default and is meant for ANSI-capable interactive
terminals.

Reuse a searcher when making repeated calls:

``` r
searcher <- sassy_searcher("dna")
sassy_searcher_search(searcher, "ATCGATCG", "GGGGATCGATCGTTTT", k = 1)
#> <sassy_matches> 3 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand  cigar
#>           0        0          4       12             0           8    0      +     8=
#>           0        0          6       14             0           8    1      - 1=1X6=
#>           0        0          2       10             0           8    1      -   7=1X
```

Vector inputs search every pattern against every text. Native builds can
use Rust/Rayon threads. The current Rsassy wasm32/webR build path keeps
the same API but runs the bulk loop serially until threaded webR
execution is explicitly enabled and validated.

``` r
sassy_search(
  c("ATG", "TTT"),
  "CCCCATGCCCCTTT",
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        7             0           3    0      +    3=
#>           1        0         11       14             0           3    0      +    3=
```

`mode = "encoded_patterns"` (alias `"v2"`) is the R equivalent of CLI
`--v2` for many equal-length short patterns. `mode = "batch_patterns"`
and `mode = "encoded_patterns"` currently require `alphabet = "iupac"`
and equal pattern byte lengths.

Connections can be searched without an R-level read loop and return the
same match columns with stream-relative coordinates:

``` r
tmp <- tempfile(fileext = ".gz")
con <- gzfile(tmp, "wb")
writeBin(charToRaw("CCCCATGCCCCTTT"), con)
close(con)

con <- gzfile(tmp, "rb")
sassy_search_connection(c("ATG", "TTT"), con, k = 1, alphabet = "iupac", rc = FALSE, mode = "encoded_patterns")
#> <sassy_matches> 2 matches
#> pattern_idx text_idx text_start text_end pattern_start pattern_end cost strand cigar
#>           0        0          4        7             0           3    0      +    3=
#>           1        0         11       14             0           3    0      +    3=
close(con)
unlink(tmp)
```

Inspect the installed build:

``` r
features <- sassy_features()
data.frame(
  feature = rep(names(features), lengths(features)),
  value = unlist(features, use.names = FALSE),
  row.names = NULL
)
#>                           feature            value
#> 1                 rsassy_dispatch          dynamic
#> 2         rsassy_selected_backend             avx2
#> 3       rsassy_installed_backends           scalar
#> 4       rsassy_installed_backends             avx2
#> 5       rsassy_installed_backends           avx512
#> 6       rsassy_supported_backends           scalar
#> 7       rsassy_supported_backends             avx2
#> 8                        cpu_avx2             TRUE
#> 9                     cpu_avx512f            FALSE
#> 10                       cpu_neon            FALSE
#> 11            rsassy_rust_version 0.2.1-0.1.0.9000
#> 12                    target_arch           x86_64
#> 13                      target_os            linux
#> 14          selected_simd_backend             avx2
#> 15       selected_portable_scalar            FALSE
#> 16           selected_native_simd             TRUE
#> 17         selected_compiled_avx2             TRUE
#> 18      selected_compiled_avx512f            FALSE
#> 19         selected_compiled_neon            FALSE
#> 20 selected_compiled_wasm_simd128            FALSE
```

For backend benchmarks, run each backend in a separate R process because
the backend is loaded once per process and remains fixed for the
lifetime of that process. Select a backend explicitly with
`sassy_set_backend()` before any other native Rsassy call, or use the
default auto-selection. Rsassy does not unload and replace backend DLLs
because that is not reliable across R platforms. This chunk launches
independent `Rscript` processes and reads one CSV row from each. Text
construction and warm-up happen outside the timed section, which scans
about 2.5 GB per backend by default.

``` r
bench_backend <- function(backend, reps = 50L, n_bases = 50000000L) {
  script <- tempfile(fileext = ".R")
  out <- tempfile(fileext = ".csv")
  writeLines(c(
    "args <- commandArgs(trailingOnly = TRUE)",
    "backend <- args[[1L]]",
    "reps <- as.integer(args[[2L]])",
    "n_bases <- as.integer(args[[3L]])",
    "out <- args[[4L]]",
    "library(Rsassy)",
    "sassy_set_backend(backend)",
    "pattern <- paste(rep('ACGT', 8L), collapse = '')",
    "chunk <- paste(rep(c('TTTTCCCCAAAAGGGG', 'CCCTTTTGGGGAAAAA', 'GGGGAAAATTTTCCCC', 'AAAAGGGGCCCCTTTT'), 16L), collapse = '')",
    "filler <- paste(rep(chunk, ceiling(1000000L / nchar(chunk))), collapse = '')",
    "unit <- paste0(substr(filler, 1L, 1000000L - nchar(pattern)), pattern)",
    "text <- paste(rep(unit, ceiling(n_bases / nchar(unit))), collapse = '')",
    "text <- substr(text, 1L, n_bases)",
    "warmup <- sassy_search(pattern, text, 2, alphabet = 'dna', rc = FALSE)",
    "invisible(gc())",
    "hits <- 0L",
    "elapsed <- system.time(for (i in seq_len(reps)) hits <- hits + nrow(sassy_search(pattern, text, 2, alphabet = 'dna', rc = FALSE)))[['elapsed']]",
    "scanned_mb <- as.numeric(n_bases) * reps / 1e6",
    "write.csv(data.frame(requested_backend = backend, selected_backend = sassy_features()$rsassy_selected_backend, text_mb = n_bases / 1e6, searches = reps, scanned_mb = scanned_mb, matches_per_search = nrow(warmup), elapsed_seconds = elapsed, mb_per_second = scanned_mb / elapsed), out, row.names = FALSE)"
  ), script)
  on.exit(unlink(c(script, out)), add = TRUE)
  status <- system2(
    file.path(R.home("bin"), "Rscript"),
    c("--vanilla", script, backend, reps, n_bases, out)
  )
  if (!identical(status, 0L)) {
    stop("backend benchmark failed for ", backend, call. = FALSE)
  }
  read.csv(out, stringsAsFactors = FALSE)
}

has_backend <- function(name) {
  name %in% features$rsassy_supported_backends
}
bench_backends <- c(
  "scalar",
  if (isTRUE(features$cpu_avx2) && has_backend("avx2")) "avx2",
  if (isTRUE(features$cpu_avx512f) && has_backend("avx512")) "avx512",
  if (isTRUE(features$cpu_neon) && has_backend("neon")) "neon"
)
bench <- do.call(rbind, lapply(bench_backends, bench_backend))
bench$speedup_vs_scalar <- bench$elapsed_seconds[match("scalar", bench$selected_backend)] /
  bench$elapsed_seconds
bench
#>   requested_backend selected_backend text_mb searches scanned_mb
#> 1            scalar           scalar      50       50       2500
#> 2              avx2             avx2      50       50       2500
#>   matches_per_search elapsed_seconds mb_per_second speedup_vs_scalar
#> 1                 50           1.232      2029.221           1.00000
#> 2                 50           0.663      3770.739           1.85822
```

## Check

``` sh
cd r/Rsassy
make check
```

Useful targets include `make readme`, `make rd`, `make install`,
`make test`, and `make clean`. `make check` uses a CRAN-safe default of
two Cargo build jobs; use `make check-fast` or
`make CARGO_JOBS=10 check` for local multithreaded Cargo builds.
