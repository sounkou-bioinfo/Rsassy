# Searching R Connections

[`sassy_search_connection()`](https://sounkou-bioinfo.github.io/Rsassy/reference/sassy_search_connection.md)
searches an already-open readable R connection. This avoids an R-level
[`readBin()`](https://rdrr.io/r/base/readBin.html) loop and keeps a
native overlap window so matches can cross chunk boundaries.

## Basic workflow

``` r

tmp <- tempfile(fileext = ".gz")
con <- gzfile(tmp, "wb")
writeBin(charToRaw("CCCCATGCCCCTTT"), con)
close(con)

con <- gzfile(tmp, "rb")
sassy_search_connection(
  c("ATG", "TTT"),
  con,
  k = 1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
close(con)
unlink(tmp)
```

The returned coordinates are relative to the full stream, not to
individual chunks.

## Chunking and overlap

`chunk_size` controls how many new bytes are read per native chunk. If
`overlap = NULL`, the C shim computes a conservative overlap from the
longest pattern, `k`, and the overhang setting.

``` r

con <- rawConnection(charToRaw("AAAAACGTAAAA"), "rb")
sassy_search_connection(
  "ACGT",
  con,
  k = 0,
  alphabet = "dna",
  rc = FALSE,
  chunk_size = 6
)
close(con)
```

Open compressed connections, such as
[`gzfile()`](https://rdrr.io/r/base/connections.html), in binary mode
when possible.
