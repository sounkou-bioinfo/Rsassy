# Report Rsassy build and CPU feature information

Returns diagnostic information about the selected Rsassy backend.
Calling this initializes the native backend if it has not already been
loaded. `rsassy_selected_backend` reports the runtime-selected backend.
`rsassy_installed_backends` is a character vector of backend libraries
found in the package installation, and `rsassy_supported_backends` is
the subset supported by the current CPU/runtime. With `"auto"`
selection, Rsassy chooses the best supported installed backend: AVX-512
before AVX2 on x86_64, NEON on arm64, WebAssembly SIMD128 on wasm, and
scalar otherwise. The `selected_*` fields describe the loaded Rust
backend. The `cpu_*` fields are detected by the C shim.

## Usage

``` r
sassy_features()
```

## Value

A `sassy_features` list of build, selected-backend, and CPU/runtime
feature values.

## Examples

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
