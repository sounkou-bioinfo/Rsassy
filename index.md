# Rsassy

[![R-universe](https://sounkou-bioinfo.r-universe.dev/badges/Rsassy)](https://sounkou-bioinfo.r-universe.dev/Rsassy)

R bindings to [Sassy](https://github.com/RagnarGrootKoerkamp/sassy)
through R’s native C API. Results are returned as data frames with
0-based coordinates and CIGAR strings.

## Install

Install from r-universe:

``` r

install.packages(
  "Rsassy",
  repos = c("https://sounkou-bioinfo.r-universe.dev", "https://cloud.r-project.org")
)
```

Source installs require Cargo/rustc \>= 1.91 and `xz`. Rust crates are
vendored in `src/rust/vendor.tar.xz` for offline package builds. On
Linux, macOS, and Windows, Rsassy installs multiple backend libraries
when possible: scalar, AVX2, and AVX-512 on x86_64; scalar and NEON on
arm64. The webR/WebAssembly build uses wasm SIMD128; see the [browser
demo](https://sounkou-bioinfo.github.io/sassy-webR/). Rsassy selects the
best installed backend supported by the current CPU/runtime when the
backend is first loaded.

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

Vector inputs search every pattern against every text. For larger
batches, use `threads > 1`.

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
`--v2` for many equal-length short patterns. `batch_patterns` and
`encoded_patterns` use Sassy’s multi-pattern encoding, which in `sassy`
0.2.1 is implemented for IUPAC and equal byte-length patterns. Use
`single` for other alphabets or mixed pattern lengths.

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

sassy_features()
#> <sassy_features>
#> dispatch: dynamic
#> selected backend: avx2
#> installed backends: scalar, avx2, avx512
#> supported backends: scalar, avx2
#> CPU: avx2=yes avx512f=no neon=no
#> Rust backend: avx2 (native_simd=yes)
```

Backend loading is one-shot per R process. If you need to benchmark or
debug a specific backend, call
[`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
before the first native Rsassy call in a fresh `Rscript` process. See
[`vignette("backend-selection", package = "Rsassy")`](https://sounkou-bioinfo.github.io/Rsassy/articles/backend-selection.md)
for the details.

## Development

Common development commands from the repository root:

``` sh
make vendor-rust  # refresh src/rust/vendor.tar.xz after Rust dependency changes
make rd           # regenerate NAMESPACE and man/*.Rd from R/search.R
make readme       # regenerate README.md from README.Rmd
make install      # install the package locally
make test         # run tinytest tests
make check        # build and run R CMD check
make clean        # remove generated build artifacts
```

`make check` uses a CRAN-safe default of two Cargo build jobs. Use
`make check-fast` or `make CARGO_JOBS=10 check` for local multithreaded
Cargo builds.
