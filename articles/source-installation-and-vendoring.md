# Development and Vendoring

Rsassy builds a small Rust crate during package installation. That crate
depends on `sassy` from crates.io.

``` toml
sassy = { version = "0.2.1", default-features = false }
```

## Vendoring

Rust sources are installed from the vendored bundle. Refresh it after
Rust dependency changes:

``` sh
make vendor-rust
```

This updates:

``` text
src/rust/vendor.tar.xz
inst/AUTHORS
inst/LICENCE.note
```

Package installation then runs Cargo in offline mode.

## Development commands

``` sh
make rd       # regenerate NAMESPACE and man/*.Rd
make readme   # regenerate README.md
make install  # local install
make test     # tinytest
make check    # R CMD build + R CMD check
make clean    # remove build artifacts
```

`make check` uses two Cargo jobs by default. For local builds:

``` sh
make CARGO_JOBS=10 check
```

## R build hooks

- `configure` / `configure.win` generate `src/Makevars` files.
- `src/Makevars.in` / `src/Makevars.win.in` build the Rust libraries.
- `src/install.libs.R` installs backend libraries under `backends/`.
- `cleanup` removes generated files.
