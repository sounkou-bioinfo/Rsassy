#!/usr/bin/env Rscript

args <- commandArgs(trailingOnly = TRUE)
sassy <- if (length(args)) args[[1L]] else Sys.which("sassy")
if (!nzchar(sassy) || !file.exists(sassy)) {
  stop("usage: Rscript tools/update-cli-fixtures.R /path/to/sassy", call. = FALSE)
}

root <- normalizePath(file.path(getwd()), mustWork = TRUE)
if (!file.exists(file.path(root, "DESCRIPTION"))) {
  stop("run from the Rsassy package root", call. = FALSE)
}
out_dir <- file.path(root, "inst", "extdata", "cli-conformance")
dir.create(out_dir, recursive = TRUE, showWarnings = FALSE)

write_fasta <- function(records, path) {
  con <- file(path, open = "wb")
  on.exit(close(con), add = TRUE)
  for (id in names(records)) {
    writeLines(paste0(">", id), con = con, useBytes = TRUE)
    writeLines(unname(records[[id]]), con = con, useBytes = TRUE)
  }
  invisible(path)
}

run_cli <- function(arguments, stdout = TRUE) {
  out_file <- tempfile("sassy-cli-stdout-")
  err_file <- tempfile("sassy-cli-stderr-")
  on.exit(unlink(c(out_file, err_file)), add = TRUE)

  status <- system2(sassy, arguments, stdout = out_file, stderr = err_file)
  out <- readLines(out_file, warn = FALSE)
  err <- readLines(err_file, warn = FALSE)
  if (!identical(status, 0L)) {
    cat("sassy CLI stdout:\n", paste(out, collapse = "\n"), "\n", sep = "")
    cat("sassy CLI stderr:\n", paste(err, collapse = "\n"), "\n", sep = "")
    stop("sassy CLI failed with status ", status, call. = FALSE)
  }
  if (stdout) out else invisible(out)
}

empty_search <- function() {
  data.frame(
    pat_id = character(), text_id = character(), cost = integer(), strand = character(),
    start = integer(), end = integer(), match_region = character(), cigar = character(),
    stringsAsFactors = FALSE
  )
}

read_search_output <- function(lines) {
  if (!length(lines)) {
    return(empty_search())
  }
  utils::read.delim(
    text = paste(lines, collapse = "\n"),
    stringsAsFactors = FALSE,
    check.names = FALSE
  )
}

normalize_search <- function(x) {
  cols <- names(empty_search())
  x <- as.data.frame(x)[cols]
  x$cost <- as.integer(x$cost)
  x$start <- as.integer(x$start)
  x$end <- as.integer(x$end)
  for (col in setdiff(cols, c("cost", "start", "end"))) {
    x[[col]] <- as.character(x[[col]])
  }
  if (nrow(x)) {
    x <- x[do.call(order, x), , drop = FALSE]
  }
  row.names(x) <- NULL
  x
}

empty_crispr <- function() {
  data.frame(
    guide = character(), text_id = character(), cost = integer(), strand = character(),
    start = integer(), end = integer(), match_region = character(), cigar = character(),
    stringsAsFactors = FALSE
  )
}

normalize_crispr <- function(x) {
  cols <- names(empty_crispr())
  x <- as.data.frame(x)[cols]
  x$cost <- as.integer(x$cost)
  x$start <- as.integer(x$start)
  x$end <- as.integer(x$end)
  for (col in setdiff(cols, c("cost", "start", "end"))) {
    x[[col]] <- as.character(x[[col]])
  }
  if (nrow(x)) {
    x <- x[do.call(order, x), , drop = FALSE]
  }
  row.names(x) <- NULL
  x
}

write_expected <- function(x, path) {
  utils::write.table(x, path, sep = "\t", quote = FALSE, row.names = FALSE, col.names = TRUE)
}

patterns <- c(p_atg = "ATG", p_ttt = "TTT")
texts <- c(t1 = "CCCCATGCCCCTTT")
pattern_fa <- write_fasta(patterns, file.path(out_dir, "patterns.fa"))
text_fa <- write_fasta(texts, file.path(out_dir, "texts.fa"))

search_lines <- run_cli(c(
  "search", "--pattern-fasta", pattern_fa, "-k", "1", "--no-rc", "--alphabet", "iupac", text_fa
))
write_expected(normalize_search(read_search_output(search_lines)), file.path(out_dir, "expected-search.tsv"))

v2_lines <- run_cli(c(
  "search", "--pattern-fasta", pattern_fa, "-k", "1", "--no-rc", "--alphabet", "iupac", "--v2", text_fa
))
write_expected(normalize_search(read_search_output(v2_lines)), file.path(out_dir, "expected-search-v2.tsv"))

sam_texts <- c(chr1 = "TTTCGTTT")
sam_fa <- write_fasta(sam_texts, file.path(out_dir, "sam.fa"))
sam_lines <- run_cli(c(
  "search", "-p", "ACGA", "-k", "0", "--alphabet", "dna", "--sam", sam_fa
))
write_expected(normalize_search(read_search_output(sam_lines)), file.path(out_dir, "expected-search-sam.tsv"))

guides <- "ACGTNGG"
writeLines(guides, file.path(out_dir, "guides.txt"), useBytes = TRUE)
crispr_texts <- c(chr1 = "TTTACGTAGGTTT", chr2 = "TTTACGTAAATTT")
crispr_fa <- write_fasta(crispr_texts, file.path(out_dir, "crispr.fa"))
crispr_out <- tempfile(fileext = ".tsv")
run_cli(c(
  "crispr", "--guide", file.path(out_dir, "guides.txt"), "-k", "2", "--max-n-frac", "0.2",
  "--no-rc", "--threads", "1", "--output", crispr_out, crispr_fa
), stdout = FALSE)
write_expected(normalize_crispr(utils::read.delim(crispr_out, stringsAsFactors = FALSE, check.names = FALSE)), file.path(out_dir, "expected-crispr.tsv"))
unlink(crispr_out)

message("Wrote CLI fixtures to ", out_dir)
