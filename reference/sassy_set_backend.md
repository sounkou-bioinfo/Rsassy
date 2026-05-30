# Select the Rsassy native backend

Select a backend for the current R process. Backend loading is
intentionally one-shot: the selected shared library is fixed for the
lifetime of the R process. This must be called before the first native
Rsassy operation, including
[`sassy_features()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_features.md),
[`sassy_searcher()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_searcher.md),
or
[`sassy_search()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search.md).
Rsassy does not unload and replace backend DLLs because that is not
reliable across R platforms. Use this for benchmarking installed
backends against each other in separate fresh R processes.

## Usage

``` r
sassy_set_backend(
  backend = c("auto", "scalar", "avx2", "avx512", "neon", "wasm_simd128")
)
```

## Arguments

- backend:

  One of `"auto"`, `"scalar"`, `"avx2"`, `"avx512"`, `"neon"`, or
  `"wasm_simd128"`.

## Value

The requested backend name, invisibly. `"auto"` means runtime dispatch
will choose the best installed backend supported by the current
CPU/runtime when the backend is first loaded.
