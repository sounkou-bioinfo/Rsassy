#!/usr/bin/env Rscript

args <- commandArgs(trailingOnly = TRUE)
venv <- if (length(args)) args[[1L]] else ".venv"
venv <- normalizePath(venv, mustWork = TRUE)

library(reticulate)
use_virtualenv(venv, required = TRUE)
py_sassy <- import("sassy")
builtins <- import_builtins()

library(Rsassy)

py_bytes <- function(x) builtins$bytes(x, "ascii")

empty_matches <- function() {
  data.frame(
    pattern_idx = integer(),
    text_idx = integer(),
    text_start = integer(),
    text_end = integer(),
    pattern_start = integer(),
    pattern_end = integer(),
    cost = integer(),
    strand = character(),
    cigar = character(),
    stringsAsFactors = FALSE
  )
}

py_matches_df <- function(matches) {
  if (!length(matches)) return(empty_matches())
  out <- do.call(rbind, lapply(matches, function(m) {
    data.frame(
      pattern_idx = as.integer(m$pattern_idx),
      text_idx = as.integer(m$text_idx),
      text_start = as.integer(m$text_start),
      text_end = as.integer(m$text_end),
      pattern_start = as.integer(m$pattern_start),
      pattern_end = as.integer(m$pattern_end),
      cost = as.integer(m$cost),
      strand = as.character(m$strand),
      cigar = as.character(m$cigar),
      stringsAsFactors = FALSE
    )
  }))
  row.names(out) <- NULL
  out
}

normalize_matches <- function(x) {
  cols <- names(empty_matches())
  x <- as.data.frame(x)[cols]
  for (col in setdiff(cols, c("strand", "cigar"))) {
    x[[col]] <- as.integer(x[[col]])
  }
  x$strand <- as.character(x$strand)
  x$cigar <- as.character(x$cigar)
  if (nrow(x)) {
    x <- x[do.call(order, x), , drop = FALSE]
  }
  row.names(x) <- NULL
  x
}

expect_same <- function(label, r_matches, py_matches) {
  r <- normalize_matches(r_matches)
  p <- normalize_matches(py_matches_df(py_matches))
  if (!identical(r, p)) {
    cat("R matches:\n")
    print(r)
    cat("Python matches:\n")
    print(p)
    stop("conformance mismatch: ", label, call. = FALSE)
  }
  message("PASS ", label)
}

# One-off searches.
expect_same(
  "dna forward search",
  sassy_search(list("ACGT"), list("TTACGTAA"), 0, alphabet = "dna", rc = FALSE),
  py_sassy$Searcher("dna", rc = FALSE)$search(py_bytes("ACGT"), py_bytes("TTACGTAA"), 0L)
)

expect_same(
  "dna reverse-complement search",
  sassy_search(list("ATCGATCG"), list("GGGGATCGATCGTTTT"), 1, alphabet = "dna", rc = TRUE),
  py_sassy$Searcher("dna", rc = TRUE)$search(py_bytes("ATCGATCG"), py_bytes("GGGGATCGATCGTTTT"), 1L)
)

expect_same(
  "ascii search",
  sassy_search(list("hello"), list("world hello there"), 0, alphabet = "ascii"),
  py_sassy$Searcher("ascii")$search(py_bytes("hello"), py_bytes("world hello there"), 0L)
)

# All end positions.
expect_same(
  "search_all",
  sassy_search(list("ATCGATCG"), list("GGGGATCGATCGTTTT"), 2, alphabet = "dna", rc = FALSE, all = TRUE),
  py_sassy$Searcher("dna", rc = FALSE)$search_all(py_bytes("ATCGATCG"), py_bytes("GGGGATCGATCGTTTT"), 2L)
)

# Cartesian product search.
patterns <- c("hello", "world")
texts <- c("hello world", "the world wide web")
expect_same(
  "search_many pairwise",
  sassy_search(as.list(patterns), as.list(texts), 0, alphabet = "ascii", strategy = "pairwise", threads = 2L),
  py_sassy$Searcher("ascii")$search_many(
    lapply(patterns, py_bytes),
    lapply(texts, py_bytes),
    0L,
    2L,
    "single"
  )
)

message("PASS Rsassy/Python conformance")
