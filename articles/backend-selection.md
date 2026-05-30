# Backend Selection

Rsassy installs a C shim plus Rust backend libraries. One backend is
selected per R process.

## Auto selection

`backend = "auto"` uses the first installed and supported backend in
this order:

1.  `wasm_simd128`
2.  `avx512`
3.  `avx2`
4.  `neon`
5.  `scalar`

``` r

sassy_features()
#> <sassy_features>
#> dispatch: dynamic
#> selected backend: avx512
#> installed backends: scalar, avx2, avx512
#> supported backends: scalar, avx2, avx512
#> CPU: avx2=yes avx512f=yes neon=no
#> Rust backend: avx512f (native_simd=yes)
```

## Explicit selection

Call
[`sassy_set_backend()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_set_backend.md)
before any other Rsassy native call. Backend loading is one-shot, so
test another backend in a new R process.

``` r

script <- tempfile(fileext = ".R")
writeLines(c(
  "library(Rsassy)",
  "sassy_set_backend('scalar')",
  "print(sassy_features())"
), script)
cat(system2(file.path(R.home("bin"), "Rscript"), c("--vanilla", script), stdout = TRUE), sep = "\n")
#> <sassy_features>
#> dispatch: dynamic
#> selected backend: scalar
#> installed backends: scalar, avx2, avx512
#> supported backends: scalar, avx2, avx512
#> CPU: avx2=yes avx512f=yes neon=no
#> Rust backend: portable_scalar (native_simd=no)
unlink(script)
```

## Why separate backend libraries?

The `sassy` crate uses build-time SIMD configuration and SIMD-dependent
types. The R package therefore builds separate Rust libraries for the
supported backend families instead of switching individual functions at
runtime.
